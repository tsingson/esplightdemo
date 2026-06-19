package main

import (
	"bytes"
	"fmt"
	"log"
	"os"
	"time"

	"go.bug.st/serial"
)

func main() {
	portName := "/dev/cu.ESP32-LED-Controller"

	mode := &serial.Mode{
		BaudRate: 115200,
	}

	// 1. 打开蓝牙虚拟串口
	port, err := serial.Open(portName, mode)
	if err != nil {
		log.Fatalf("【错误】无法打开蓝牙串口 %s: %v", portName, err)
	}
	defer port.Close()

	fmt.Printf("[*] 成功唤醒系统蓝牙端口: %s\n", portName)
	fmt.Println("[*] 正在与 ESP32 进行无线链路握手...")
	time.Sleep(2 * time.Second) // 留足空中建链时间

	cmd := "9"
	if len(os.Args) > 1 {
		cmd = os.Args[1]
	}

	// 2. 发送指令
	_, err = port.Write([]byte(cmd))
	if err != nil {
		log.Fatalf("【错误】发送指令失败: %v", err)
	}
	fmt.Printf("[->] 已发送指令: %s\n", cmd)

	// 3. 【核心修复】：手动时间戳硬超时机制（彻底免疫 macOS 驱动 Bug）
	startTime := time.Now()
	var response []byte
	buf := make([]byte, 128)

	for {
		n, err := port.Read(buf)
		if err != nil {
			log.Fatalf("【错误】读取数据时发生异常: %v", err)
		}

		if n > 0 {
			// 读到数据，追加到响应池
			response = append(response, buf[:n]...)

			// 完美对齐：一旦捕获到 ESP32 发出的 \n，说明接收完整，立即破圈
			if len(response) > 0 && response[len(response)-1] == '\n' {
				break
			}
		} else {
			// n == 0：说明当前系统缓冲区暂时没收到新字节
			// 检查总耗时是否真正超过了 5 秒
			if time.Since(startTime) > 5*time.Second {
				fmt.Println("[!] 接收超时：在 5 秒内未收到 ESP32 的完整有效响应。")
				return
			}
			// 极其重要：让出 CPU，防止非阻塞状态下 Go 协程空转导致单核 CPU 飙满 100%
			time.Sleep(40 * time.Millisecond)
		}
	}

	// 4. 打印最终结果
	cleanResult := string(bytes.TrimSpace(response))
	fmt.Printf("[<-] 收到 ESP32 回应: %s\n", cleanResult)
}