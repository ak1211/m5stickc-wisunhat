#!/usr/bin/env python3
# pip install boto3
import os
import sys
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import math
import itertools
from matplotlib.dates import DayLocator, HourLocator, MinuteLocator, DateFormatter
from pytz import timezone
from datetime import datetime, timedelta
from dateutil import parser
import boto3
from boto3.dynamodb.conditions import Key

DEVICE_ID = "m5-WiSUN"
SENSOR_ID = "smartmeter"
TABLE_NAME = "measurements"


def time_sequential_data_frame(item_list, tz):
    pairs = [('sensor_id', lambda x:x),
             ('measured_at', lambda x:parser.parse(x).astimezone(tz)),
             ('cumlative_kwh', lambda x: float(x) if x is not None else None),
             ('instant_watt', lambda x: int(x) if x is not None else None),
             ('instant_ampere_R', lambda x: float(x) if x is not None else None),
             ('instant_ampere_T', lambda x: float(x) if x is not None else None)]
    columns, _ = zip(*pairs)

    def pickup(item):
        return [func(item.get(col)) for (col, func) in pairs]
    #
    data = [pickup(item) for item in item_list]
    df = pd.DataFrame(data=data, columns=columns)
    return df


def take_items_from_table(table, begin, end, tz):
    begin_ = math.floor(begin.astimezone(timezone('UTC')).timestamp())
    end_ = math.ceil(end.astimezone(timezone('UTC')).timestamp())
    print("{} -> {}".format(begin, end))

    params = {
        'KeyConditionExpression': Key('device_id').eq(DEVICE_ID) & Key('timestamp').between(begin_, end_),
        'FilterExpression': Key('data.sensor_id').eq(SENSOR_ID),
    }
    response = table.query(**params)
    item_list = response['Items']

    while 'LastEvaluatedKey' in response:
        params['ExclusiveStartKey'] = response['LastEvaluatedKey']
        response = table.query(**params)
        item_list += response['Items']

    return time_sequential_data_frame([x["data"] for x in item_list], tz)


def plot(df, filename_png, filename_csv, tz):
    #
    df = df.set_index('measured_at')
    # print(df)
    df.to_csv(filename_csv)
    #
    major_formatter = DateFormatter('%a\n%Y-%m-%d\n%H:%M:%S\n%Z', tz=tz)
    major_locator = DayLocator(tz=tz)
    minor_formatter = DateFormatter('%H:%M', tz=tz)
    # 1時間単位
    # minor_locator = HourLocator(byhour=range(0, 24, 1), tz=tz)
    # 30分単位
    minor_locator = MinuteLocator(byminute=range(0, 24*60, 30), tz=tz)
    #
    xlim = [df.index[0].astimezone(tz).replace(
        hour=0, minute=0, second=0, microsecond=0), df.index[-1]]
    #
    fig, axs = plt.subplots(3, 1, figsize=(48, 24))
    #
    axs[0].xaxis.set_major_locator(major_locator)
    axs[0].xaxis.set_major_formatter(major_formatter)
    axs[0].xaxis.set_minor_locator(minor_locator)
    axs[0].xaxis.set_minor_formatter(minor_formatter)
    axs[0].set_xlim(xlim)
    axs[0].set_ylabel('kWh')
    axs[0].set_title('cumulative amounts of electric power', fontsize=18)
    v = df['cumlative_kwh'].dropna()
    x = v.index.tolist()
    y = v.tolist()
    if (len(y) > 2):
        axs[0].set_ylim((v.min(), v.max()))
    # 折れ線グラフとx軸の間を塗りつぶす
    # axs[0].fill_between(x, y, color="lightblue", alpha=1.0)
    # 折れ線グラフ
    axs[0].plot(v, color="blue", marker='o', clip_on=False)
    # 30分の幅
    width = 30/(24*60)
    # 棒グラフ
    axs[0].bar(x, y, width=width, color="lightblue", align="edge")
    axs[0].grid(which='both', axis='both')
    #
    axs[1].xaxis.set_major_locator(major_locator)
    axs[1].xaxis.set_major_formatter(major_formatter)
    axs[1].xaxis.set_minor_locator(minor_locator)
    axs[1].xaxis.set_minor_formatter(minor_formatter)
    axs[1].set_xlim(xlim)
    axs[1].set_ylabel('W')
    axs[1].set_title('instantaneous electric power', fontsize=18)
    x = df['instant_watt'].dropna().index.tolist()
    y = df['instant_watt'].dropna().tolist()
    # 折れ線グラフ
#    axs[1].plot(x, v, color="blue", marker='o', clip_on=False)
    # 1分の幅
    width = 1/(24*60)
    # 棒グラフ
    axs[1].bar(x, y, width=width, color="blue", align="edge")
    axs[1].grid(which='both', axis='both')
    peak_index = np.argmax(y)
    peak = y[peak_index]
    axs[1].annotate(' {}\n {:.0f} W'.format(datetime.strftime(x[peak_index], '%H:%M:%S as %Z'), peak),
                    xy=(x[peak_index], peak),
                    size=15,
                    xytext=(xlim[-1], peak+1),
                    color='red',
                    arrowprops=dict(color="red", arrowstyle="wedge,tail_width=1."))
    #
    axs[2].xaxis.set_major_locator(major_locator)
    axs[2].xaxis.set_major_formatter(major_formatter)
    axs[2].xaxis.set_minor_locator(minor_locator)
    axs[2].xaxis.set_minor_formatter(minor_formatter)
    axs[2].set_xlim(xlim)
    axs[2].set_ylabel('A')
    axs[2].set_title(
        'instantaneous electric current', fontsize=18)
    x = df['instant_ampere_R'].dropna().index.tolist()
    r = df['instant_ampere_R'].dropna().tolist()
    t = df['instant_ampere_T'].dropna().tolist()
    r_plus_t = [a+b for (a, b) in zip(r, t)]
    # 折れ線グラフ
#    axs[2].stackplot(x, r, t, colors=['lightcoral', 'lightblue'], alpha=1.0,
#                     labels = ['R-phase', 'T-phase'])
#    axs[2].plot(x, r, color="maroon", marker='o', clip_on=False)
#    axs[2].plot(x, r_plus_t, color="blue", marker='o', clip_on=False)
    # 積み上げ棒グラフ
    # 1分の幅
    width = 1/(24*60)
    # R相電流
    axs[2].bar(x, r, width=width, color="tomato",
               align="edge", label="R-phase")
    # T相電流
    axs[2].bar(x, t, width=width, color="blue",
               align="edge", label="T-phase", bottom=r)
    axs[2].legend(loc='upper left')
    axs[2].grid(which='both', axis='both')
    peak_index = np.argmax(r_plus_t)
    axs[2].annotate(' {}\n {:.1f} A'.format(datetime.strftime(x[peak_index], '%H:%M:%S as %Z'), r_plus_t[peak_index]),
                    xy=(x[peak_index], r_plus_t[peak_index]),
                    size=15,
                    xytext=(xlim[-1], r_plus_t[peak_index]+1),
                    color='red',
                    arrowprops=dict(color="red", arrowstyle="wedge,tail_width=1."))
    #
#    fig.tight_layout()
    fig.savefig(filename_png)
    plt.close()


def take_first_and_last_items(table):
    params = {
        'KeyConditionExpression': Key('device_id').eq(DEVICE_ID),
        'FilterExpression': Key('data.sensor_id').eq(SENSOR_ID),
        'Limit': 1,
        'ScanIndexForward': True
    }
    params['ScanIndexForward'] = True
    response = table.query(**params)
    first = response['Items'][0]
    params['ScanIndexForward'] = False
    response = table.query(**params)
    last = response['Items'][0]

    return (first['data'], last['data'])


def date_sequence(begin, end):
    t = begin
    while t <= end:
        yield t
        t += timedelta(days=1)


def split_weekly(begin, end):
    def attach_weeknumber(ds):
        return [(d.isocalendar()[1], d) for d in ds]

    def key(keyvalue):
        (k, v) = keyvalue
        return k

    def value(keyvalue):
        (k, v) = keyvalue
        return v
    ds = date_sequence(begin, end)
    xs = attach_weeknumber(ds)
    xxs = itertools.groupby(xs, key)
    for _, group in xxs:
        yield [value(kv) for kv in group]


def split_dayly(begin, end):
    def attach_daynumber(ds):
        return [(d.isocalendar()[2], d) for d in ds]

    def key(keyvalue):
        (k, v) = keyvalue
        return k

    def value(keyvalue):
        (k, v) = keyvalue
        return v
    ds = date_sequence(begin, end)
    xs = attach_daynumber(ds)
    xxs = itertools.groupby(xs, key)
    for _, group in xxs:
        yield [value(kv) for kv in group]


def run(region_name, aws_access_key_id, aws_secret_access_key):
    dynamodb = boto3.resource(
        'dynamodb', region_name=region_name, aws_access_key_id=aws_access_key_id, aws_secret_access_key=aws_secret_access_key)
    table = dynamodb.Table(TABLE_NAME)
    #
    tz = timezone('Asia/Tokyo')
    (first_item, last_item) = take_first_and_last_items(table)
    #
    first = parser.parse(first_item.get("measured_at")).astimezone(
        tz).replace(hour=0, minute=0, second=0, microsecond=0)
    #
    last = parser.parse(last_item.get("measured_at")).astimezone(
        tz).replace(hour=23, minute=59, second=59, microsecond=999999)

    daylies = split_dayly(first, last)
    for day in daylies:
        begin = day[0]
        end = day[-1] + timedelta(days=1) - timedelta(microseconds=1)
        begin_ = begin.strftime('%Y-%m-%dT%H%M')
        end_ = end.strftime('%H%M')
        filename_png = "{}to{}.png".format(begin_, end_)
        filename_csv = "{}to{}.csv".format(begin_, end_)
        # 同名のファイルがあれば何もしない
        if (os.path.isfile(filename_png)):
            print("file {} is already exist, pass".format(filename_png))
        else:
            df = take_items_from_table(table, begin, end, tz)
            plot(df, filename_png, filename_csv, tz)
            print("----------")


if __name__ == '__main__':
    if len(sys.argv) <= 3:
        print("$ {} region_name aws_access_key_id aws_secret_access_key".format(
            sys.argv[0]))
    else:
        region_name = sys.argv[1]
        aws_access_key_id = sys.argv[2]
        aws_secret_access_key = sys.argv[3]
        run(region_name, aws_access_key_id, aws_secret_access_key)
