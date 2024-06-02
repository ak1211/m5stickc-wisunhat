# m5stickc-wisunhat
M5StickCPlusとWi-SUN HATでスマートメータから情報を得る

M5Stick-CとWi-SUN HAT(BP35A1)を接続する。

## 接続情報を用意する
WiFiとスマートメータールートＢとAWS IoT Coreの接続情報をjson形式で書いて`data/settings.json`ファイルに保存する。  
(`data/`ディレクトリがない場合は作る。)

接続情報例(data/settings.json)
```
{
    "wifi": {
        "SSID": "************",
        "password": "********"
    },
    "RouteB": {
        "id": "********************************",
        "password": "************"
    },
    "DeviceId": "********",
    "SensorId": "**********",
    "AwsIoT": {
        "Endpoint": "***************************************************",
        "root_ca_file": "/AmazonRootCA1.pem",
        "certificate_file": "/certificate.pem.crt",
        "private_key_file": "/private.pem.key"
    }
}
```
DeviceIdはMQTT publish / subscribe 識別用(自由に決めればよい)
SensorIdはDynamo DBのパーティションキー(自由に決めればよい)

AWS IoTからダウンロードした証明書ファイル

- AmazonRootCA1.pem("-----BEGIN CERTIFICATE-----"から始まるファイル)
- certificate.pem.crt("-----BEGIN CERTIFICATE-----"から始まるファイル)
- private.pem.key("-----BEGIN RSA PRIVATE KEY-----"から始まるファイル)

以上のAWS IoT 証明書ファイルを`data/`におく。  

AWS IoTからダウンロードした証明書ファイル(public.pem.keyは不要)は長いのでリネームする。  

- `AmazonRootCA1.pem`
- `6*************************************************************76-certificate.pem.crt`
- `6*************************************************************76-private.pem.key`  

## 接続情報の書込み
PlatformIO で Upload Filesystem Image すると`data/`ディレクトリの内容がM5Stickにアップロードされる。

## ファームウエアの書込み
PlatformIO で Build & Upload する。

AWS IoTページのMQTTテストクライアントで `device/+/data`をサブスクライブすると、これが発行するMQTT通信が(うまくいっていると)見られる。
