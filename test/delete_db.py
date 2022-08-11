#!/usr/bin/env python3
#
# $ pip3 install azure-cosmos
#
# Copyright (c) 2021 Akihiro Yamamoto.
# Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
# See LICENSE file in the project root for full license information.

from tkinter import W
from azure.cosmos import CosmosClient, exceptions
import sys
from pytz import timezone
from datetime import datetime
from dateutil import parser

if __name__ == "__main__":
    if len(sys.argv) <= 2:
        print("{} url key".format(sys.argv[0]))
    else:
        url = sys.argv[1]
        key = sys.argv[2]
        client = CosmosClient(url, credential=key)
        database_name = "ThingsDatabase"
        container_name = "Measurements"
        database = client.get_database_client(database_name)
        container = database.get_container_client(container_name)
        #
        sensorId = 'smartmeter'
        #
        item_list = list(container.query_items(
            query="SELECT c.id FROM c WHERE c.sensorId=@sid",
            parameters=[
                {"name": "@sid", "value": sensorId},
            ],
            enable_cross_partition_query=True))
        #
        for delete_item_id in item_list:
            print("Delete item id={}".format(delete_item_id))
            try:
                container.delete_item(
                    item=delete_item_id['id'], partition_key=sensorId)
            except exceptions.CosmosResourceNotFoundError:
                pass
