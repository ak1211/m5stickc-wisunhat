//
// テレメトリのCSVファイルから, 積算電力量/瞬時電力値/瞬時電流をプロットする
//
// Copyright (c) 2023 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
use anyhow::anyhow;
use chrono::{Duration, NaiveDateTime, NaiveTime, TimeZone};
use chrono_tz::Asia::Tokyo;
use chrono_tz::Tz;
use clap::Parser;
use plotters::prelude::*;
use polars::lazy::dsl::{Expr, StrptimeOptions};
use polars::lazy::prelude::*;
use polars::prelude::PolarsError::{ComputeError, NoData};
use polars::prelude::*;
use std::ffi::OsString;
use std::fs;
use std::fs::DirEntry;
use std::ops::Range;
use std::path::{Path, PathBuf};

mod colname {
    pub const MEASURED_AT: &'static str = "measured_at";
    pub const CUMLATIVE_KWH: &'static str = "cumlative_kwh";
    pub const INSTANT_WATT: &'static str = "instant_watt";
    pub const INSTANT_AMPERE_R: &'static str = "instant_ampere_R";
    pub const INSTANT_AMPERE_T: &'static str = "instant_ampere_T";
}

// 積算電力量測定値を得る
fn get_cumlative_kwh(ldf: LazyFrame) -> LazyFrame {
    ldf.select([col(colname::MEASURED_AT), col(colname::CUMLATIVE_KWH)])
        .filter(col(colname::MEASURED_AT).is_not_null())
        .filter(col(colname::CUMLATIVE_KWH).is_not_null())
}

// 瞬時電力値を得る
fn get_instant_watt(ldf: LazyFrame) -> LazyFrame {
    ldf.select([col(colname::MEASURED_AT), col(colname::INSTANT_WATT)])
        .filter(col(colname::MEASURED_AT).is_not_null())
        .filter(col(colname::INSTANT_WATT).is_not_null())
}

// R相瞬時電流値を得る
fn get_instant_ampere_r(ldf: LazyFrame) -> LazyFrame {
    ldf.select([col(colname::MEASURED_AT), col(colname::INSTANT_AMPERE_R)])
        .filter(col(colname::MEASURED_AT).is_not_null())
        .filter(col(colname::INSTANT_AMPERE_R).is_not_null())
}

// T相瞬時電流値を得る
fn get_instant_ampere_t(ldf: LazyFrame) -> LazyFrame {
    ldf.select([col(colname::MEASURED_AT), col(colname::INSTANT_AMPERE_T)])
        .filter(col(colname::MEASURED_AT).is_not_null())
        .filter(col(colname::INSTANT_AMPERE_T).is_not_null())
}

// 瞬時電流値を得る
fn get_instant_ampere(ldf: LazyFrame) -> LazyFrame {
    let ampere_r: LazyFrame = get_instant_ampere_r(ldf.clone());
    let ampere_t: LazyFrame = get_instant_ampere_t(ldf);

    // T相電流は無い場合がある
    ampere_r.join(
        ampere_t,
        [col(colname::MEASURED_AT)],
        [col(colname::MEASURED_AT)],
        JoinType::Left,
    )
}

// CSVファイルを読み込んでデータフレームを作る
fn read_csv<P: AsRef<Path>>(path: P) -> Result<LazyFrame, PolarsError> {
    let ldf = LazyCsvReader::new(path).has_header(true).finish()?;

    // measured_at列をstr型からdatetime型に変換する
    let expr: Expr = col(colname::MEASURED_AT)
        .str()
        .strptime(
            DataType::Datetime(TimeUnit::Milliseconds, None),
            StrptimeOptions {
                format: Some("%Y-%m-%dT%H:%M:%S%z".into()),
                strict: false,
                ..Default::default()
            },
        )
        .alias(colname::MEASURED_AT); // 変換済みの列で上書きする

    Ok(ldf.with_column(expr))
}

// X軸の日付時間
fn as_datetime_vector(
    series: &Series,
    tz: Tz,
) -> Result<(Vec<NaiveDateTime>, RangedDateTime<NaiveDateTime>), PolarsError> {
    // SeriesからNaiveDateTime(UTC)に変換する
    let original_datetimes = series
        .datetime()?
        .as_datetime_iter()
        .collect::<Option<Vec<NaiveDateTime>>>()
        .ok_or(ComputeError("datetime parse error".into()))?;
    // UTCからLocalに変換する
    let local_datetimes = original_datetimes
        .iter()
        .map(|t| tz.from_utc_datetime(t).naive_local())
        .collect::<Vec<NaiveDateTime>>();
    // 開始時間
    let start_datetime = local_datetimes
        .iter()
        .min()
        .ok_or(NoData("datetime".into()))?;
    // 終了時間
    let end_datetime = local_datetimes
        .iter()
        .max()
        .ok_or(NoData("datetime".into()))?;
    // 開始日の午前0時
    let day_start_datetime = NaiveDateTime::new(
        start_datetime.date(),
        NaiveTime::from_hms_opt(0, 0, 0).unwrap(),
    );
    // 終了日の翌日の午前0時
    let day_end_datetime = NaiveDateTime::new(
        end_datetime.date() + Duration::days(1),
        NaiveTime::from_hms_opt(0, 0, 0).unwrap(),
    );
    // 時間の範囲
    let range_datetime: Range<NaiveDateTime> = day_start_datetime..day_end_datetime;
    //
    Ok((local_datetimes, range_datetime.into()))
}

// Y軸の値
fn as_values_vector(series: &Series) -> Result<(Vec<f64>, Range<f64>), PolarsError> {
    // 値
    let values = series
        .iter()
        .map(|x| x.try_extract())
        .collect::<Result<Vec<f64>, _>>()?;
    // 最大値
    let max_value = values
        .iter()
        .max_by(|a, b| a.total_cmp(b))
        .ok_or(NoData("value".into()))?;
    // 最小値
    let min_value = values
        .iter()
        .min_by(|a, b| a.total_cmp(b))
        .ok_or(NoData("value".into()))?;
    // 値の範囲
    let range_value = (*min_value)..(*max_value);
    Ok((values, range_value))
}

// 積算電力量グラフを作る
fn plot_cumlative_kilo_watt_hour<DB: DrawingBackend>(
    area: &DrawingArea<DB, plotters::coord::Shift>,
    df: &DataFrame,
    line_style: ShapeStyle,
    box_style: ShapeStyle,
) -> anyhow::Result<()>
where
    DB::ErrorType: 'static,
{
    // X軸の日付時間
    let (datetimes, range_datetime) = as_datetime_vector(&df[colname::MEASURED_AT], Tokyo)?;
    // Y軸の測定値
    let (values, range_value) = as_values_vector(&df[colname::CUMLATIVE_KWH])?;
    // XYの値
    let dataset: Vec<(&NaiveDateTime, &f64)> = datetimes.iter().zip(values.iter()).collect();
    //
    let mut chart = ChartBuilder::on(area)
        .caption("積算電力量測定値(30分値)", ("sans-serif", 16).into_font())
        .margin(10)
        .x_label_area_size(40)
        .y_label_area_size(60)
        .build_cartesian_2d(range_datetime.clone(), range_value)?;
    // 軸ラベルとか
    chart
        .configure_mesh()
        .x_labels(24)
        .x_label_formatter(&|t: &NaiveDateTime| t.format("%H").to_string())
        .x_desc(range_datetime.range().start.format("%F %A").to_string())
        .y_desc("積算電力量30分値(kWh)")
        .draw()?;
    // 積算電力量を高さと30分の横幅の四角で表現する
    let rectangles = dataset.iter().copied().map(|(datetime, value)| {
        let start_x = *datetime;
        let end_x = start_x.checked_add_signed(Duration::minutes(30)).unwrap();
        let mut bar = Rectangle::new([(start_x, 0.0), (end_x, *value)], box_style);
        bar.set_margin(0, 0, 0, 0);
        bar
    });
    chart.draw_series(rectangles)?;
    // 積算電力量を折れ線で表現する
    chart.draw_series(LineSeries::new(
        dataset
            .iter()
            .copied()
            .map(|(datetime, value)| (*datetime, *value)),
        line_style,
    ))?;
    // 積算電力量を点で表現する
    chart.draw_series(
        dataset
            .iter()
            .copied()
            .map(|(x, y)| Circle::new((*x, *y), 2, line_style)),
    )?;

    Ok(())
}

// 瞬時電力グラフを作る
fn plot_instant_watt<DB: DrawingBackend>(
    area: &DrawingArea<DB, plotters::coord::Shift>,
    df: &DataFrame,
    box_style: ShapeStyle,
) -> anyhow::Result<()>
where
    DB::ErrorType: 'static,
{
    // X軸の日付時間
    let (datetimes, range_datetime) = as_datetime_vector(&df[colname::MEASURED_AT], Tokyo)?;
    // Y軸の測定値
    let (values, range_value) = as_values_vector(&df[colname::INSTANT_WATT])?;
    // XYの値
    let dataset: Vec<(&NaiveDateTime, &f64)> = datetimes.iter().zip(values.iter()).collect();
    //
    let mut chart = ChartBuilder::on(area)
        .caption("瞬時電力測定値(1分値)", ("sans-serif", 16).into_font())
        .margin(10)
        .x_label_area_size(40)
        .y_label_area_size(60)
        .build_cartesian_2d(
            range_datetime.clone(),
            range_value.start.min(0.0)..range_value.end,
        )?;
    // 軸ラベルとか
    chart
        .configure_mesh()
        .x_labels(24)
        .x_label_formatter(&|t: &NaiveDateTime| t.format("%H").to_string())
        .x_desc(range_datetime.range().start.format("%F %A").to_string())
        .y_desc("瞬時電力1分値(W)")
        .draw()?;
    // 瞬時電力量を高さと1分の横幅の四角で表現する
    chart.draw_series(dataset.iter().copied().map(|(datetime, value)| {
        let start_x = *datetime;
        let end_x = start_x.checked_add_signed(Duration::minutes(1)).unwrap();
        let mut bar = Rectangle::new([(start_x, 0.0), (end_x, *value)], box_style);
        bar.set_margin(0, 0, 0, 0);
        bar
    }))?;

    Ok(())
}

// 瞬時電流グラフを作る
fn plot_instant_ampere<DB: DrawingBackend>(
    area: &DrawingArea<DB, plotters::coord::Shift>,
    df: &DataFrame,
    r_box_style: ShapeStyle,
    t_box_style: ShapeStyle,
) -> anyhow::Result<()>
where
    DB::ErrorType: 'static,
{
    // X軸の日付時間
    let (datetimes, range_datetime) = as_datetime_vector(&df[colname::MEASURED_AT], Tokyo)?;
    // Y軸のR相電流測定値
    let (values_r, _) = as_values_vector(&df[colname::INSTANT_AMPERE_R])?;
    // Y軸のT相電流測定値
    let (values_t, _) = as_values_vector(&df[colname::INSTANT_AMPERE_T])?;
    // R相電流とT相電流を加算する
    let accumlated = values_r
        .iter()
        .zip(values_t.iter())
        .map(|(a, b)| a + b)
        .collect::<Vec<f64>>();
    // R相電流とT相電流を加算した値の最大値
    let max_value = accumlated
        .iter()
        .max_by(|a, b| a.total_cmp(b))
        .ok_or(NoData("datetime".into()))?;
    // R相電流とT相電流を加算した値の最小値
    let min_value = accumlated
        .iter()
        .min_by(|a, b| a.total_cmp(b))
        .ok_or(NoData("datetime".into()))?;
    // R相電流とT相電流を加算した値の範囲
    let range_value = (*min_value)..(*max_value);
    // (X, R相Y, T相Y)の値
    let dataset: Vec<(&NaiveDateTime, &f64, &f64)> =
        itertools::izip!(&datetimes, &values_r, &values_t).collect();
    //
    let mut chart = ChartBuilder::on(area)
        .caption("瞬時電流測定値(1分値)", ("sans-serif", 16).into_font())
        .margin(10)
        .x_label_area_size(40)
        .y_label_area_size(60)
        .build_cartesian_2d(
            range_datetime.clone(),
            range_value.start.min(0.0)..range_value.end,
        )?;
    // 軸ラベルとか
    chart
        .configure_mesh()
        .x_labels(24)
        .x_label_formatter(&|t: &NaiveDateTime| t.format("%H").to_string())
        .x_desc(range_datetime.range().start.format("%F %A").to_string())
        .y_desc("瞬時電流1分値(A)")
        .draw()?;
    // R相電流を高さと1分の横幅の四角で表現する
    chart
        .draw_series(dataset.iter().copied().map(|(datetime, value_r, _)| {
            let start_x = *datetime;
            let end_x = start_x.checked_add_signed(Duration::minutes(1)).unwrap();
            let start_y = 0.0; // R相電流の高さは0から始める
            let end_y = start_y + *value_r;
            let mut bar = Rectangle::new([(start_x, start_y), (end_x, end_y)], r_box_style);
            bar.set_margin(0, 0, 0, 0); // 隙間なく並べる
            bar
        }))?
        // R相電流の凡例
        .label("R相電流")
        .legend(|(x, y)| PathElement::new(vec![(x, y), (x + 20, y)], r_box_style));

    // T相電流を高さと1分の横幅の四角で表現する
    // T相電流はR相電流の上に積む
    chart
        .draw_series(dataset.iter().copied().map(|(datetime, value_r, value_t)| {
            let start_x = *datetime;
            let end_x = start_x.checked_add_signed(Duration::minutes(1)).unwrap();
            let start_y = *value_r; // T相電流の高さはR相電流の高さの分加算する
            let end_y = start_y + *value_t;
            let mut bar = Rectangle::new([(start_x, start_y), (end_x, end_y)], t_box_style);
            bar.set_margin(0, 0, 0, 0); // 隙間なく並べる
            bar
        }))?
        // T相電流の凡例
        .label("T相電流")
        .legend(|(x, y)| PathElement::new(vec![(x, y), (x + 20, y)], t_box_style));

    // 凡例
    chart
        .configure_series_labels()
        .background_style(WHITE.mix(0.5))
        .border_style(BLACK)
        .draw()?;

    Ok(())
}

// グラフを作る
fn plot<DB: DrawingBackend>(
    root_area: DrawingArea<DB, plotters::coord::Shift>,
    df: DataFrame,
) -> anyhow::Result<()>
where
    DB::ErrorType: 'static,
{
    // 背景色
    root_area.fill(&WHITE)?;
    // 縦に3分割する
    let areas = root_area.split_evenly((3, 1));
    if let [one, two, three] = &areas[..3] {
        // 積算電力量グラフを作る
        let cumlative_kwh: DataFrame = get_cumlative_kwh(df.clone().lazy()).collect()?;
        plot_cumlative_kilo_watt_hour(one, &cumlative_kwh, BLUE.filled(), BLUE.mix(0.2).filled())?;
        // 瞬時電力グラフを作る
        let instant_watt: DataFrame = get_instant_watt(df.clone().lazy()).collect()?;
        plot_instant_watt(two, &instant_watt, BLUE.mix(0.8).filled())?;
        // 瞬時電流グラフを作る
        let instant_ampere: DataFrame = get_instant_ampere(df.clone().lazy()).collect()?;
        plot_instant_ampere(
            three,
            &instant_ampere,
            MAGENTA.mix(0.8).filled(),
            BLUE.mix(0.8).filled(),
        )?;
    } else {
        panic!("fatal error")
    }
    // プロット完了
    root_area.present()?;

    Ok(())
}

#[derive(Debug, Copy, Clone, PartialEq)]
enum ChartFileType {
    Png,
    Svg,
}

// csvファイルからグラフを作る
fn run<P: AsRef<Path>>(
    infilepath: P,
    overwrite: bool,
    plotareasize: (u32, u32),
    chart_file_type: ChartFileType,
) -> anyhow::Result<String> {
    // 出力するファイル名は入力ファイルの.csvを.png/.svgに変えたもの
    let infilepath_string = format!("{:?}", infilepath.as_ref().as_os_str());
    let mut outfilepath: PathBuf = PathBuf::from(infilepath.as_ref());
    outfilepath.set_extension(match chart_file_type {
        ChartFileType::Png => "png",
        ChartFileType::Svg => "svg",
    });
    // 出力するファイルの存在確認
    if outfilepath.is_file() && !overwrite {
        let outfilepath_string = format!("{:?}", outfilepath.as_os_str());
        Err(anyhow!("{} file is already exist!", outfilepath_string))?;
    }
    // CSVファイルからデーターフレームを作る
    let df: DataFrame = read_csv(infilepath)?
        .sort(colname::MEASURED_AT, SortOptions::default())
        .collect()?;
    //
    match chart_file_type {
        ChartFileType::Png => {
            let root_area = BitMapBackend::new(&outfilepath, plotareasize).into_drawing_area();
            plot(root_area, df.clone())?;
        }
        ChartFileType::Svg => {
            let root_area = SVGBackend::new(&outfilepath, plotareasize).into_drawing_area();
            plot(root_area, df.clone())?;
        }
    };
    // 結果を返す
    Ok(format!("inputfile -> {}\n{:?}", infilepath_string, df))
}

#[derive(Parser)]
#[command(author, version, about, about ="Plot chart of telemetry data", long_about = None)] // Read from `Cargo.toml`
struct Cli {
    #[arg(short = 'x', long, default_value_t = 800)]
    width: u32,
    #[arg(short = 'y', long, default_value_t = 800)]
    height: u32,
    #[arg(long)]
    png: bool,
    #[arg(long)]
    overwrite: bool,
}

fn main() -> anyhow::Result<()> {
    let cli = Cli::parse();
    //
    // カレントディレクトリ
    let dir = fs::read_dir("./")?;
    // ディレクトリのファイルリスト
    let files: Vec<DirEntry> = dir.into_iter().collect::<Result<Vec<DirEntry>, _>>()?;
    // サフィックスが"csv"のファイルリスト
    let csv_files: Vec<&DirEntry> = files
        .iter()
        .filter(|s: &&DirEntry| {
            let suffix = OsString::from("csv").to_ascii_lowercase();
            s.path().extension().map(|a| a.to_ascii_lowercase()) == Some(suffix)
        })
        .collect();
    // 出力ファイルの種類
    let chart_file_type = if cli.png {
        ChartFileType::Png
    } else {
        ChartFileType::Svg
    };
    // グラフの大きさ
    let plotareasize = (cli.width, cli.height);
    // csvファイルからグラフを作る
    for p in csv_files {
        let result = run(p.path(), cli.overwrite, plotareasize, chart_file_type)
            .unwrap_or_else(|e| format!("{:?}", e));
        println!("{}", result);
    }

    Ok(())
}
