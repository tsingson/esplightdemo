# ESP32 led blink demo

ESP32 dev module with LED blink / on / off , default is blink
control via Golang cli throut serial port

```
 ./bin/esp32demo
Usage: go run main.go [0|1|2|?]
  0 - Mode 0 (Turn OFF LED)
  1 - Mode 1 (Turn ON LED constantly)
  2 - Mode 2 (Blink LED every 0.5s)
  9 - Query current mode status
```

