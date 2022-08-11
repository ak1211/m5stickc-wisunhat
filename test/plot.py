#!/usr/bin/env python3
#
# $ pip3 install azure-cosmos
#
# Copyright (c) 2022 Akihiro Yamamoto.
# Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
# See LICENSE file in the project root for full license information.

from azure.cosmos import CosmosClient
import os
import sys
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import math
import itertools
from matplotlib.dates import DayLocator, HourLocator, DateFormatter
from pytz import timezone
from datetime import datetime, timedelta
from dateutil import parser


def time_sequential_data_frame(item_list, tz):
    pairs = [('sensorId', lambda x:x),
             ('measuredAt', lambda x:parser.parse(x).astimezone(tz)),
             ('cumlativeKwh', lambda x: float(x) if x is not None else None),
             ('instantWatt', lambda x: int(x) if x is not None else None),
             ('instantAmpereR', lambda x: float(x) if x is not None else None),
             ('instantAmpereT', lambda x: float(x) if x is not None else None)]
    columns, _ = zip(*pairs)

    def pickup(item):
        return [func(item.get(col)) for (col, func) in pairs]
    #
    data = [pickup(item) for item in item_list]
    df = pd.DataFrame(data=data, columns=columns)
    return df


def take_items_from_container(container, begin, end, tz):
    begin_ = begin.astimezone(timezone('UTC')).isoformat()
    end_ = end.astimezone(timezone('UTC')).isoformat()
    print("{} -> {}".format(begin, end))
    item_list = list(container.query_items(
        query="SELECT * FROM c WHERE c.sensorId='smartmeter' AND (c.measuredAt BETWEEN @begin AND @end)",
        parameters=[
            {"name": "@begin", "value": begin_},
            {"name": "@end", "value": end_}
        ],
        enable_cross_partition_query=True))
    return time_sequential_data_frame(item_list, tz)


def plot(df, filename, tz):
    #
    df = df.set_index('measuredAt')
    print(df)
    #
    major_formatter = DateFormatter('%a\n%Y-%m-%d\n%H:%M:%S\n%Z', tz=tz)
    major_locator = DayLocator(tz=tz)
    minor_formatter = DateFormatter('%H', tz=tz)
    minor_locator = HourLocator(byhour=range(0, 24, 1), tz=tz)
    #
    xlim = [df.index[0].astimezone(tz).replace(hour=0, minute=0, second=0, microsecond=0),
            df.index[-1]]
    #
    fig, axs = plt.subplots(4, 1, figsize=(32, 16))
    #
    axs[0].xaxis.set_major_locator(major_locator)
    axs[0].xaxis.set_major_formatter(major_formatter)
    axs[0].xaxis.set_minor_locator(minor_locator)
    axs[0].xaxis.set_minor_formatter(minor_formatter)
    axs[0].set_xlim(xlim)
    axs[0].set_ylabel('kWh')
    axs[0].set_title('cumulative amounts of electric power', fontsize=18)
    axs[0].plot(df['cumlativeKwh'].dropna(), 's-')
    axs[0].grid(which='both', axis='both')
    #
    axs[1].xaxis.set_major_locator(major_locator)
    axs[1].xaxis.set_major_formatter(major_formatter)
    axs[1].xaxis.set_minor_locator(minor_locator)
    axs[1].xaxis.set_minor_formatter(minor_formatter)
    axs[1].set_xlim(xlim)
    axs[1].set_ylabel('W')
    axs[1].set_title('instantaneous electric power', fontsize=18)
    axs[1].plot(df['instantWatt'].dropna(), 's-')
    axs[1].grid(which='both', axis='both')
    #
    axs[2].xaxis.set_major_locator(major_locator)
    axs[2].xaxis.set_major_formatter(major_formatter)
    axs[2].xaxis.set_minor_locator(minor_locator)
    axs[2].xaxis.set_minor_formatter(minor_formatter)
    axs[2].set_xlim(xlim)
    axs[2].set_ylabel('A')
    axs[2].set_title(
        'R-phase instantaneous electric current', fontsize=18)
    axs[2].plot(df['instantAmpereR'].dropna(), 's-')
    axs[2].grid(which='both', axis='both')
    #
    axs[3].xaxis.set_major_locator(major_locator)
    axs[3].xaxis.set_major_formatter(major_formatter)
    axs[3].xaxis.set_minor_locator(minor_locator)
    axs[3].xaxis.set_minor_formatter(minor_formatter)
    axs[3].set_xlim(xlim)
    axs[3].set_ylabel('A')
    axs[3].set_title(
        'T-phase instantaneous electric current', fontsize=18)
    axs[3].plot(df['instantAmpereT'].dropna(), 's-')
    axs[3].grid(which='both', axis='both')
    #
#    fig.tight_layout()
    fig.savefig(filename)


def take_first_and_last_items(container):
    def do_query(query):
        targets = list(container.query_items(
            query=query,
            enable_cross_partition_query=True))
        if len(targets) > 0:
            items = list(container.query_items(
                query="SELECT * FROM c WHERE c.sensorId='smartmeter' AND c.measuredAt=@at",
                parameters=[
                    {"name": "@at", "value": targets[0]}
                ],
                enable_cross_partition_query=True))
            return items[0]
        return None
    first = do_query(
        "SELECT VALUE(MIN(c.measuredAt)) FROM c WHERE c.sensorId='smartmeter'")
    last = do_query(
        "SELECT VALUE(MAX(c.measuredAt)) FROM c WHERE c.sensorId='smartmeter'")
    return (first, last)


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


def run(url, key):
    cosmos_client = CosmosClient(url, credential=key)
    database_name = "ThingsDatabase"
    container_name = "Measurements"
    database = cosmos_client.get_database_client(database_name)
    container = database.get_container_client(container_name)
    #
    tz = timezone('Asia/Tokyo')
    (first_item, last_item) = take_first_and_last_items(container)
    #
    first = parser.parse(first_item.get("measuredAt")).astimezone(
        tz).replace(hour=0, minute=0, second=0, microsecond=0)
    #
    last = parser.parse(last_item.get("measuredAt")).astimezone(
        tz).replace(hour=23, minute=59, second=59, microsecond=999999)

    daylies = split_dayly(first, last)
    for day in daylies:
        begin = day[0]
        end = day[-1] + timedelta(days=1) - timedelta(microseconds=1)
        begin_ = begin.strftime('%Y-%m-%dT%H%M')
        end_ = end.strftime('%H%M')
        filename = "{}to{}.png".format(begin_, end_)
        # 同名のファイルがあれば何もしない
        if(os.path.isfile(filename)):
            print("file {} is already exist, pass".format(filename))
        else:
            df = take_items_from_container(container, begin, end, tz)
            plot(df, filename, tz)
            print("----------")


if __name__ == "__main__":
    if len(sys.argv) <= 2:
        print("$ {} CosmosDBのURI CosmosDBのキー".format(sys.argv[0]))
    else:
        url = sys.argv[1]
        key = sys.argv[2]
        run(url, key)
