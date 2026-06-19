package main

import (
	"bytes"
	"fmt"
	"log"
	"os"
	"time"

	"tinygo.org/x/bluetooth"
)

var (
	adapter          = bluetooth.DefaultAdapter
	targetDeviceName = "ESP32-BLE-Controller"
)

func main() {
	nusServiceUUID, err := bluetooth.ParseUUID("6e400001-b5a3-f393-e0a9-e50e24dcca9e")
	if err != nil {
		log.Fatalf("【错误】解析 Service UUID 失败: %v", err)
	}
	rxUUID, err := bluetooth.ParseUUID("6e400002-b5a3-f393-e0a9-e50e24dcca9e")
	if err != nil {
		log.Fatalf("【错误】解析 RX UUID 失败: %v", err)
	}
	txUUID, err := bluetooth.ParseUUID("6e400003-b5a3-f393-e0a9-e50e24dcca9e")
	if err != nil {
		log.Fatalf("【错误】解析 TX UUID 失败: %v", err)
	}

	err = adapter.Enable()
	if err != nil {
		log.Fatalf("【错误】无法唤醒系统蓝牙适配器: %v", err)
	}

	fmt.Printf("[*] 正在扫描附近的 BLE 设备: %s ...\n", targetDeviceName)

	var targetAddress bluetooth.Address
	var found bool

	err = adapter.Scan(func(adapter *bluetooth.Adapter, device bluetooth.ScanResult) {
		if device.LocalName() == targetDeviceName {
			fmt.Printf("[+] 找到设备！信号强度 (RSSI): %d, MAC地址: %s\n", device.RSSI, device.Address.String())
			targetAddress = device.Address
			found = true
			adapter.StopScan()
		}
	})
	if err != nil {
		log.Fatalf("【错误】扫描异常: %v", err)
	}
	if !found {
		log.Fatalf("【错误】未找到设备 %s", targetDeviceName)
	}

	fmt.Println("[*] 正在建立 BLE 连接...")
	device, err := adapter.Connect(targetAddress, bluetooth.ConnectionParams{})
	if err != nil {
		log.Fatalf("【错误】连接设备失败: %v", err)
	}
	defer device.Disconnect()
	fmt.Println("[+] BLE 连接成功！")

	srvcs, err := device.DiscoverServices([]bluetooth.UUID{nusServiceUUID})
	if err != nil || len(srvcs) == 0 {
		log.Fatalf("【错误】未找到对应的 UART 服务: %v", err)
	}
	srvc := srvcs[0]

	chars, err := srvc.DiscoverCharacteristics(nil)
	if err != nil {
		log.Fatalf("【错误】检索特征值失败: %v", err)
	}

	var rxChar, txChar *bluetooth.DeviceCharacteristic
	for i := range chars {
		currUUIDStr := chars[i].UUID().String()
		if currUUIDStr == rxUUID.String() {
			rxChar = &chars[i]
		}
		if currUUIDStr == txUUID.String() {
			txChar = &chars[i]
		}
	}

	if rxChar == nil || txChar == nil {
		log.Fatalf("【错误】通道建立失败，RX/TX 特征值不完整。")
	}
	fmt.Println("[+] RX/TX 双向隔空数据通道建立完毕！")

	responseChan := make(chan string, 1)
	err = txChar.EnableNotifications(func(buf []byte) {
		responseChan <- string(bytes.TrimSpace(buf))
	})
	if err != nil {
		log.Fatalf("【错误】订阅通知失败: %v", err)
	}
	fmt.Println("[*] 已成功挂载数据接收器，等待回传信号...")

	// 提取控制指令
	cmd := "2" // 默认发 2 (闪烁)
	if len(os.Args) > 1 {
		cmd = os.Args[1]
	}
	fmt.Printf("[->] 正在发射无线指令: %q\n", cmd)

	// 使用标准的带应答 Write 确保数据必达
	_, err = rxChar.Write([]byte(cmd))
	if err != nil {
		log.Fatalf("【错误】无线指令发射失败: %v", err)
	}

	// 阻塞等待回应
	select {
	case res := <-responseChan:
		// ================================================================
		// 【日志优化】：根据发送的指令，智能化打印结果
		// ================================================================
		if cmd == "0" || cmd == "1" || cmd == "2" {
			if res == "9" {
				fmt.Printf("[<-] ESP32 反馈: 状态代码 %s (操作成功，模式已成功切换！)\n", res)
			} else {
				fmt.Printf("[<-] 收到非预期回应: %s\n", res)
			}
		} else if cmd == "9" {
			fmt.Printf("[<-] ESP32 反馈: 当前运行模式为 [%s]\n", res)
		} else {
			fmt.Printf("[<-] 收到回应: %s\n", res)
		}

	case <-time.After(3 * time.Second):
		fmt.Println("[!] 接收超时：发射成功，但未收到 ESP32 的回传数据。")
	}
}
