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
 	// ================================================================
 	// 【macOS 核心修正 1】：改用 tty 节点
 	// macOS 的 /dev/tty.xxx 节点在经典蓝牙下会严格维持硬件载波信号。
 	// 如果运行提示找不到设备，请先确认 Mac 蓝牙系统设置里它处于“已连接”状态。
 	// 如果 tty 依然报错，可尝试将其切回下方的 "/dev/cu.ESP32-LED-Controller"
 	// ================================================================
 	portName := "/dev/tty.ESP32-LED-Controller"

 	mode := &serial.Mode{
 		BaudRate: 115200,
 	}

 	// 1. 打开蓝牙虚拟串口
 	fmt.Printf("[*] 正在呼叫 macOS 蓝牙串行端口: %s ...\n", portName)
 	port, err := serial.Open(portName, mode)
 	if err != nil {
 		log.Fatalf("【错误】无法打开蓝牙端口 %s: %v\n👉 提示：请确保 Mac 蓝牙列表中设备显示“已连接”。", portName, err)
 	}
 	defer port.Close()

 	fmt.Println("[+] 端口已成功建立物理映射！")

 	// ================================================================
 	// 【macOS 核心修正 2】：强行向驱动注入 DTR 和 RTS 高电平控制信号
 	// 彻底告警 macOS 蓝牙驱动：“客户端已就绪”，强行逼迫其激活并放行 RFCOMM 空中数据流
 	// ================================================================
 	fmt.Println("[*] 正在向 macOS 驱动发送 DTR/RTS 链路激活信号...")
 	if err := port.SetDTR(true); err != nil {
 		log.Printf("[!] 警告: 设置 DTR 失败 (部分驱动限制): %v", err)
 	}
 	if err := port.SetRTS(true); err != nil {
 		log.Printf("[!] 警告: 设置 RTS 失败 (部分驱动限制): %v", err)
 	}

 	// ================================================================
 	// 【macOS 核心修正 3】：给空中信道留出绝对充裕的稳定时间
 	// ESP32 端触发“连接成功”回调后，Mac 端的驱动状态机往往还需要几百毫秒完成最后握手
 	// 此时立刻 Write 会直接导致数据死在 Mac 缓冲区里。这里我们稳稳等待 2 秒
 	// ================================================================
 	fmt.Println("[*] 正在等待无线空中信道完全稳定 (2秒)...")
 	time.Sleep(2 * time.Second)

 	// 提取外部输入指令
 	cmd := "9"
 	if len(os.Args) > 1 {
 		cmd = os.Args[1]
 	}

 	// ================================================================
 	// 【macOS 核心修正 4】：强制追加 \n 换行符
 	// 击碎 macOS 驱动的行缓冲限制，逼迫系统立刻将当前数据从 Mac 射频天线发射出去
 	// ================================================================
 	sendData := cmd + "\n"
 	_, err = port.Write([]byte(sendData))
 	if err != nil {
 		log.Fatalf("【错误】向无线通道写入指令失败: %v", err)
 	}
 	fmt.Printf("[->] 已成功发射指令流: %q\n", sendData)

 	// 2. 轮询读取响应
 	fmt.Println("[*] 正在等待 ESP32 的无线回传...")
 	startTime := time.Now()
 	var response []byte
 	buf := make([]byte, 128)

 	for {
 		n, err := port.Read(buf)
 		if err != nil {
 			log.Fatalf("【错误】从蓝牙通道读取数据异常: %v", err)
 		}

 		if n > 0 {
 			response = append(response, buf[:n]...)
 			// 接收匹配：一旦捕获到 ESP32 发回的 \n 结束符，立即收工破圈
 			if len(response) > 0 && response[len(response)-1] == '\n' {
 				break
 			}
 		} else {
 			// n == 0：说明当前系统缓冲区暂时没有收到新字节
 			// 设置 6 秒硬超时判定
 			if time.Since(startTime) > 6*time.Second {
 				fmt.Println("[!] 接收超时：空中信道虽通，但未捕获到 ESP32 的回传数据。")
 				fmt.Println("👉 请检查 PlatformIO 串口监视器，看它有没有打印出 [Debug BT Input] 日志。")
 				return
 			}
 			// 核心防御：让出 30ms 时间片，防止 Go 协程在非阻塞空转中把 Mac CPU 飙到 100%
 			time.Sleep(30 * time.Millisecond)
 		}
 	}

 	// 3. 打印最终结果
 	cleanResult := string(bytes.TrimSpace(response))
 	fmt.Printf("[<-] 收到 ESP32 无线回应: %s\n", cleanResult)
 }