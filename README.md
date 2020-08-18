# KasaEnergyLogger
Discover TP-Link Kasa power devices on local network that can monitor energy. Query the discovered devices at regular intervals and log the energy usage. Pull data in MRTG standard format.

## Currently Supported
 - HS110(US)
 - HS300(US)

## Example Log Entries
### HS110
{"date":"2020-08-16 19:06:07","deviceId":"8006D28F7D6C1FC75E7254E4D10B1D1219A9B81D",{"emeter":{"get_realtime":{"current":9.759913,"voltage":119.775026,"power":1122.805283,"total":13.406000,"err_code":0}}}}
### HS300 Top Level
{"date":"2020-08-16 19:08:07","deviceId":"8006C12BF70963C01E916C3F54E742CC1C0B3FAB",{"emeter":{"get_realtime":{"voltage_mv":121686,"current_ma":4,"power_mw":6,"total_wh":9,"err_code":0}}}}
### HS300 Child Plug
{"date":"2020-08-16 19:07:07","deviceId":"8006C12BF70963C01E916C3F54E742CC1C0B3FAB05",{"emeter":{"get_realtime":{"voltage_mv":120509,"current_ma":64,"power_mw":5222,"total_wh":28774,"err_code":0}}}}
