#!/usr/bin/env python3
#
# Copyright (c) 2022 Akihiro Yamamoto.
# Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
# See LICENSE file in the project root for full license information.
import os
import sys
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import glob
from matplotlib.dates import DayLocator, HourLocator, MinuteLocator, DateFormatter
from pytz import timezone
from datetime import datetime, timedelta
from dateutil import parser

def plot(df, filename_png, tz):
    #
    df = df.set_index('measured_at')
    # print(df)
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


def run():
    for input_filename in glob.glob("./*.csv"):
        filename_png = os.path.splitext(input_filename)[0] + ".png"
        print(input_filename)
        print(filename_png)
        # 同名のファイルがあれば何もしない
        if (os.path.isfile(filename_png)):
            print("file {} is already exist, pass".format(filename_png))
        else:
            df = pd.read_csv(input_filename, dtype = {
                'measured_at':'object',
                'sensor_id':'object',
                'message_id':        'float64',
                'cumlative_kwh':     'float64',
                'instant_watt':      'float64',
                'instant_ampere_R':  'float64',
                'instant_ampere_T':  'float64',
                })
            tz = timezone('Asia/Tokyo')
            df["measured_at"]= pd.to_datetime(df["measured_at"])
            plot(df, filename_png, tz)

if __name__ == '__main__':
    run()
