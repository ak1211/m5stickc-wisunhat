//
// DynamoDBに格納されているテレメトリを取得して, プロット用のCSVファイルを作る
//
// 接続情報は ~/.aws/credentials ファイルに用意してください。
//
// [default]
// aws_access_key_id=YOUR-ACCESS-KEY
// aws_secret_access_key=YOUR-SECRET-KEY
//
// 詳細は https://docs.aws.amazon.com/sdk-for-rust/latest/dg/credentials.html です。
//
// Copyright (c) 2023 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
use anyhow::anyhow;
use aws_config::meta::region::RegionProviderChain;
use aws_sdk_dynamodb::types::AttributeValue;
use aws_sdk_dynamodb::Client;
use chrono::Timelike;
use chrono::{DateTime, Days, Duration, FixedOffset, NaiveDateTime, NaiveTime, TimeZone};
use chrono_tz::{Asia, Tz};
use clap::Parser;
use polars::prelude::DataFrame;
use polars::prelude::Series;
use polars::prelude::*;
use std::collections::HashMap;
use std::ops::{Range, RangeInclusive};
use std::path::PathBuf;

const DEVICE_ID: &'static str = "m5-WiSUN";
const SENSOR_ID: &'static str = "smartmeter";
const TABLE_NAME: &'static str = "measurements";

// DynamoDBのAttributeValues(NまたはS)をstrで取得する。
fn get_attribute_values_str<'a>(
    item: &'a HashMap<String, AttributeValue>,
    name: &'a str,
) -> anyhow::Result<Option<&'a str>> {
    let error = |attr: &AttributeValue| anyhow!("not of type 'S' or 'N' ->  {:?}", attr);
    //
    match item.get(name) {
        // 属性値のデータ型 S
        Some(attribute_value) if attribute_value.is_s() => {
            let s = attribute_value.as_s().or_else(|e| Err(error(e)))?;
            Ok(Some(s.as_str()))
        }
        // 属性値のデータ型 N
        Some(attribute_value) if attribute_value.is_n() => {
            let s = attribute_value.as_n().or_else(|e| Err(error(e)))?;
            Ok(Some(s.as_str()))
        }
        // それ以外の属性値は解釈できないのでエラー
        Some(attribute_value) => Err(error(attribute_value)),
        // マップのキーであるnameが存在しないのはNaNなので正常
        None => Ok(None),
    }
}

// DynamoDBのHashMapの配列からpolars::prelude::Seriesを作る
fn series_from_items(
    items: &Vec<HashMap<String, AttributeValue>>,
    (name, dtype): &(&str, polars::prelude::DataType),
) -> anyhow::Result<polars::prelude::Series> {
    //
    let xs: Vec<Option<&str>> = items
        .iter()
        .map(|hm| get_attribute_values_str(hm, name))
        .collect::<anyhow::Result<Vec<Option<&str>>>>()?;
    let series = Series::new(name, xs).cast(dtype)?;
    Ok(series)
}

//
fn parse_iso8601_to_jst(s: &str) -> anyhow::Result<DateTime<FixedOffset>> {
    let fixed = DateTime::parse_from_rfc3339(s)
        .or_else(|e| Err(anyhow!("ParseError: {:?}. input is \"{}\"", e, s)))?;
    let jst = fixed.with_timezone(&Asia::Tokyo).fixed_offset();
    Ok(jst)
}

// dataframe
fn time_sequential_dataframe(
    items: Vec<HashMap<String, AttributeValue>>,
) -> anyhow::Result<polars::prelude::DataFrame> {
    // データ型の指定
    const PAIR: [(&'static str, DataType); 7] = [
        ("measured_at", DataType::Utf8),
        ("sensor_id", DataType::Utf8),
        ("message_id", DataType::UInt32),
        ("cumlative_kwh", DataType::Float64),
        ("instant_watt", DataType::Float64),
        ("instant_ampere_R", DataType::Float64),
        ("instant_ampere_T", DataType::Float64),
    ];
    // 並び順
    let ording = PAIR.iter().map(|p| p.0).collect::<Vec<&str>>();
    //
    let series = PAIR
        .iter()
        .map(|pair| series_from_items(&items, pair))
        .collect::<anyhow::Result<Vec<Series>>>()?;

    // 列を作る
    let df = DataFrame::new(series)?;
    // 列を並び変える
    let df = df.select(ording)?;
    // measured_at列をiso8601形式にする
    let datetimes = df["measured_at"]
        .utf8()?
        .into_iter()
        .map(|opt| {
            opt.and_then(|s| {
                // パースに失敗したらNone(NaN)値にする
                match parse_iso8601_to_jst(s) {
                    Ok(jst) => Some(jst),
                    Err(e) => {
                        eprintln!("WARNING: \"{:?}\" discarded.", e);
                        None
                    }
                }
            })
        })
        .collect::<Vec<Option<DateTime<FixedOffset>>>>();
    // 秒の情報は不要なので
    let datetimes = datetimes
        .iter()
        .map(|opt| {
            opt.map(|v| {
                let naivetime = v.naive_local();
                let sec = Duration::seconds(naivetime.second() as i64);
                let nsec = Duration::nanoseconds(naivetime.nanosecond() as i64);
                let dt: DateTime<FixedOffset> = v - sec - nsec;
                dt
            })
        })
        .collect::<Vec<Option<DateTime<FixedOffset>>>>();

    let datetimes_str = datetimes
        .iter()
        .map(|opt_dt| opt_dt.map(|dt| dt.to_rfc3339()))
        .collect::<Vec<Option<String>>>();
    let datetime_series = Series::new("measured_at", datetimes_str).cast(&DataType::Utf8)?;
    //
    let mut df2 = df.to_owned();
    let df = df2.with_column(datetime_series).map(|a| a.to_owned())?;
    Ok(df)
}

// DynamoDBより最初と最後のレコードを得る
async fn get_first_and_last_item(
    client: &Client,
) -> anyhow::Result<(AttributeValue, AttributeValue)> {
    let query = client
        .query()
        .table_name(TABLE_NAME)
        .key_condition_expression("device_id = :device_id")
        .filter_expression("#data.sensor_id = :sensor_id")
        .expression_attribute_names("#data", "data")
        .expression_attribute_values(":device_id", AttributeValue::S(DEVICE_ID.to_owned()))
        .expression_attribute_values(":sensor_id", AttributeValue::S(SENSOR_ID.to_owned()))
        .limit(1);

    let responce_first_item = query.clone().scan_index_forward(true).send().await?;
    let responce_last_item = query.scan_index_forward(false).send().await?;

    let opt_first_item = responce_first_item.items().and_then(|a| a.first());
    let opt_last_item = responce_last_item.items().and_then(|a| a.first());

    if let (Some(first), Some(last)) = (opt_first_item, opt_last_item) {
        let result = (first["data"].to_owned(), last["data"].to_owned());
        Ok(result)
    } else {
        Err(anyhow!("error: take_first_and_last_items()"))
    }
}

// timestamp指定でDBの"data"レコードを得る
async fn get_items_from_table(
    client: &Client,
    timestamp: Range<i64>,
) -> anyhow::Result<Vec<HashMap<String, AttributeValue>>> {
    let query = client
        .query()
        .table_name(TABLE_NAME)
        .key_condition_expression("device_id = :device_id AND #timestamp BETWEEN :tstart AND :tend")
        .filter_expression("#data.sensor_id = :sensor_id")
        .expression_attribute_names("#data", "data")
        .expression_attribute_names("#timestamp", "timestamp")
        .expression_attribute_values(":device_id", AttributeValue::S(DEVICE_ID.to_owned()))
        .expression_attribute_values(":sensor_id", AttributeValue::S(SENSOR_ID.to_owned()))
        .expression_attribute_values(":tstart", AttributeValue::N(timestamp.start.to_string()))
        .expression_attribute_values(":tend", AttributeValue::N(timestamp.end.to_string()));

    let results = query.send().await?;
    if let Some(items) = results.items {
        let data = items
            .iter()
            .map(|item| {
                item["data"]
                    .as_m()
                    .map(|hm| hm.to_owned())
                    .or(Err(anyhow!("datetime conversion error.")))
            })
            .collect::<anyhow::Result<Vec<HashMap<String, AttributeValue>>>>()?;
        Ok(data)
    } else {
        Ok(vec![])
    }
}

// "measured_at"をDateTime型で得る
fn get_measured_at(attr: &AttributeValue) -> anyhow::Result<DateTime<FixedOffset>> {
    let text = attr
        .as_m()
        .and_then(|m| m["measured_at"].as_s())
        .or_else(|e| Err(anyhow!("datetime conversion error. {:?}", e)))?;
    DateTime::parse_from_rfc3339(text)
        .or_else(|e| Err(anyhow!("datetime conversion error. {:?}", e)))
}

// 日本時間で分秒を切り捨て/切り上げる
fn jst_datetime_range(
    original: RangeInclusive<DateTime<FixedOffset>>,
) -> anyhow::Result<Range<DateTime<FixedOffset>>> {
    // 日本時間に変換する
    let start_jst: DateTime<Tz> = original.start().with_timezone(&Asia::Tokyo);
    let end_jst: DateTime<Tz> = original.end().with_timezone(&Asia::Tokyo);
    // 分秒を切り捨て/切り上げる(日本時間)
    let start_day = NaiveDateTime::new(
        start_jst.date_naive(),
        NaiveTime::from_hms_opt(0, 0, 0).unwrap(),
    );
    let end_day = NaiveDateTime::new(
        end_jst
            .date_naive()
            .checked_add_days(Days::new(1))
            .ok_or(anyhow!("datetime conversion error"))?,
        NaiveTime::from_hms_opt(0, 0, 0).unwrap(),
    );
    // 最初と最後(日本時間)
    let start_datetime_jst: DateTime<Tz> = Asia::Tokyo.from_local_datetime(&start_day).unwrap();
    let end_datetime_jst: DateTime<Tz> = Asia::Tokyo.from_local_datetime(&end_day).unwrap();
    // 変換
    let start_datetime: DateTime<FixedOffset> = start_datetime_jst.fixed_offset();
    let end_datetime: DateTime<FixedOffset> = end_datetime_jst.fixed_offset();
    //
    Ok(start_datetime..end_datetime)
}

// 開始日から最終日まで一日毎のベクタ
fn dailies(r: Range<DateTime<FixedOffset>>) -> Vec<DateTime<FixedOffset>> {
    let mut xs = Vec::new();
    let mut dt = r.start;
    while dt < r.end {
        xs.push(dt);
        dt = dt + Duration::days(1);
    }
    xs
}

// DynamoDBよりテレメトリを取得してCSVファイルにする
async fn run(
    client: &Client,
    throttle: i32,
    lap_limits: i32,
    overwrite: bool,
) -> anyhow::Result<()> {
    // データベースに記録されている最初と最後のアイテム
    let (first_item, last_item) = get_first_and_last_item(&client).await?;
    let first_datetime = get_measured_at(&first_item)?;
    let last_datetime = get_measured_at(&last_item)?;
    println!(
        "Telemetries stored from {} to {}.",
        first_datetime, last_datetime
    );
    //
    let jst_datetime_range: Range<DateTime<FixedOffset>> =
        jst_datetime_range(first_datetime..=last_datetime)?;
    // 一日分の秒
    let one_days = Duration::hours(23) + Duration::minutes(59) + Duration::seconds(59);
    // 一日づつデーターベースからダウンロードしてCSVファイルにする
    let mut loop_counter = 0;
    for jst_day in dailies(jst_datetime_range) {
        // 回数制限の確認
        if loop_counter >= lap_limits {
            eprintln!("Exceeded the limit of {} laps.", lap_limits);
            break;
        }
        // 日本時間
        let begin_datetime: NaiveDateTime = jst_day.naive_local();
        let end_datetime: NaiveDateTime = begin_datetime + one_days;
        // CSVファイル名
        let begin_str = begin_datetime.format("%Y-%m-%dT%H%M").to_string();
        let end_str = end_datetime.format("%H%M").to_string();
        let filename_csv = format!("{}to{}.csv", begin_str, end_str);
        // 出力するファイル
        let outfilepath: PathBuf = PathBuf::from(filename_csv);
        let outfilepath_string = format!("{:?}", outfilepath.as_os_str());
        // 出力するファイルの存在確認
        if outfilepath.is_file() && !overwrite {
            eprintln!("{} file is already exist!, pass", outfilepath_string);
            continue;
        }
        //
        println!(
            "lap {}. start {} to fetch.",
            loop_counter + 1,
            jst_day.format("%Y-%m-%d")
        );
        // 取得するUTC時間
        let begin_utc = jst_day.naive_utc();
        let end_utc = begin_utc + one_days;
        // 取得するtimestampの範囲
        let timestamp_utc = begin_utc.timestamp()..end_utc.timestamp();
        // データベースより取得する
        let items = get_items_from_table(&client, timestamp_utc).await?;
        if items.is_empty() {
            println!("database stored telemetry data is empty");
        } else {
            println!("outputfile -> {}", outfilepath_string);
            // 取得したデータをDataFrameに変換する
            let mut df = time_sequential_dataframe(items)?;
            println!("DataFrame from DynamoDB\n{:?}", df);
            // CSVファイルに保存する
            let mut file = std::fs::File::create(outfilepath)?;
            CsvWriter::new(&mut file).finish(&mut df)?;
        }
        // 完了したらカウンターを更新する
        loop_counter = loop_counter + 1;
        // 遅くて10,000ミリ秒待ち, 最速で1ミリ秒待ち
        let permilli = (10 * throttle).clamp(0, 1000);
        let millis = (10 * (1000 - permilli)).clamp(1, 10_000);
        let duration = tokio::time::Duration::from_millis(millis as u64);
        tokio::time::sleep(duration).await;
    }

    Ok(())
}

#[derive(Parser)]
#[command(author, version, about, about ="retrieve chart data from dynamodb", long_about = None)] // Read from `Cargo.toml`
struct Cli {
    #[arg(long)]
    region: Option<String>,
    #[arg(
        short,
        long,
        default_value_t = 50,
        help = "0(🐢) to 100(🐇)大きいほど早くなる"
    )]
    throttle: i32,
    #[arg(short, long, default_value_t = 10, help = "連続で取得する回数の最大")]
    limits: i32,
    #[arg(long)]
    overwrite: bool,
}

/// Lists your DynamoDB tables in the default Region or us-east-1 if a default Region isn't set.
#[tokio::main]
async fn main() -> anyhow::Result<()> {
    let cli = Cli::parse();
    //
    let mut region_provider = RegionProviderChain::default_provider();
    if let Some(region) = cli.region {
        region_provider = region_provider.or_else(aws_sdk_dynamodb::config::Region::new(region));
    }
    region_provider = region_provider.or_else("us-east-1");
    let config = aws_config::from_env().region(region_provider).load().await;
    let client = Client::new(&config);
    //
    run(&client, cli.throttle, cli.limits, cli.overwrite)
        .await
        .unwrap_or_else(|e| eprintln!("Error -> {:?}", e));

    Ok(())
}
