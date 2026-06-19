package main

import (
	"bufio"
	"bytes"
	"fmt"
	"log"
	"os"
	"time"

	"github.com/tarm/serial"
	"tinygo.org/x/bluetooth"
)

var (
	adapter          = bluetooth.DefaultAdapter
	targetDeviceName = "ESP32-BLE-Controller"
)

func main() {
	// Start background serial log reader
	go startTarmSerialLogger("/dev/cu.usbserial-A5069RR4", 115200)

	nusServiceUUID, err := bluetooth.ParseUUID("6e400001-b5a3-f393-e0a9-e50e24dcca9e")
	if err != nil {
		log.Fatalf("[Error] Failed to parse Service UUID: %v", err)
	}
	rxUUID, err := bluetooth.ParseUUID("6e400002-b5a3-f393-e0a9-e50e24dcca9e")
	if err != nil {
		log.Fatalf("[Error] Failed to parse RX UUID: %v", err)
	}
	txUUID, err := bluetooth.ParseUUID("6e400003-b5a3-f393-e0a9-e50e24dcca9e")
	if err != nil {
		log.Fatalf("[Error] Failed to parse TX UUID: %v", err)
	}

	err = adapter.Enable()
	if err != nil {
		log.Fatalf("[Error] Failed to enable Bluetooth adapter: %v", err)
	}

	fmt.Printf("[*] Scanning for nearby BLE devices: %s ...\n", targetDeviceName)

	var targetAddress bluetooth.Address
	var found bool

	err = adapter.Scan(func(adapter *bluetooth.Adapter, device bluetooth.ScanResult) {
		if device.LocalName() == targetDeviceName {
			fmt.Printf("[+] Device found! RSSI: %d, MAC Address: %s\n", device.RSSI, device.Address.String())
			targetAddress = device.Address
			found = true
			adapter.StopScan()
		}
	})
	if err != nil {
		log.Fatalf("[Error] Scanning exception occurred: %v", err)
	}
	if !found {
		log.Fatalf("[Error] Device %s not found", targetDeviceName)
	}

	fmt.Println("[*] Establishing BLE connection...")
	device, err := adapter.Connect(targetAddress, bluetooth.ConnectionParams{})
	if err != nil {
		log.Fatalf("[Error] Failed to connect to device: %v", err)
	}
	defer device.Disconnect()
	fmt.Println("[+] BLE connection established successfully!")

	srvcs, err := device.DiscoverServices([]bluetooth.UUID{nusServiceUUID})
	if err != nil || len(srvcs) == 0 {
		log.Fatalf("[Error] Matching UART service not found: %v", err)
	}
	srvc := srvcs[0]

	chars, err := srvc.DiscoverCharacteristics(nil)
	if err != nil {
		log.Fatalf("[Error] Failed to discover characteristics: %v", err)
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
		log.Fatalf("[Error] Channel initialization failed: RX or TX characteristic is missing.")
	}
	fmt.Println("[+] RX/TX bidirectional over-the-air data channels established!")

	responseChan := make(chan string, 1)
	err = txChar.EnableNotifications(func(buf []byte) {
		responseChan <- string(bytes.TrimSpace(buf))
	})
	if err != nil {
		log.Fatalf("[Error] Failed to subscribe to notifications: %v", err)
	}
	fmt.Println("[*] Data listener mounted successfully, waiting for return signal...")

	// Extract command argument
	cmd := "2" // Default to '2' (Blink) if no argument provided
	if len(os.Args) > 1 {
		cmd = os.Args[1]
	}
	fmt.Printf("[->] Sending wireless command: %q\n", cmd)

	// Use standard blocking Write to ensure data is properly acked and delivered
	_, err = rxChar.Write([]byte(cmd))
	if err != nil {
		log.Fatalf("[Error] Failed to send wireless command: %v", err)
	}

	// Wait for the response with a timeout
	select {
	case res := <-responseChan:
		// ================================================================
		// Log Optimization: Smart printing based on the sent command
		// ================================================================
		if cmd == "0" || cmd == "1" || cmd == "2" {
			if res == "9" {
				fmt.Printf("[<-] ESP32 Response: Status Code %s (Success, mode switched!)\n", res)
			} else {
				fmt.Printf("[<-] Received unexpected response: %s\n", res)
			}
		} else if cmd == "9" {
			fmt.Printf("[<-] ESP32 Response: Current operating mode is [%s]\n", res)
		} else {
			fmt.Printf("[<-] Received response: %s\n", res)
		}

	case <-time.After(3 * time.Second):
		fmt.Println("[!] Reception timeout: Command sent successfully, but no response received from ESP32.")
	}
}

func startTarmSerialLogger(portName string, baudRate int) {
	config := &serial.Config{
		Name:        portName,
		Baud:        baudRate,
		ReadTimeout: time.Millisecond * 100, // Non-blocking timeout
	}

	port, err := serial.OpenPort(config)
	if err != nil {
		log.Printf("[Serial Error] Failed to open port %s: %v\n", portName, err)
		log.Println("[Serial Info] Please ensure PlatformIO Serial Monitor or other serial tools are closed!")
		return
	}
	defer port.Close()

	log.Printf("[Serial] Connected to %s (%d baud). Listening for ESP32 debug logs...\n", portName, baudRate)

	// Use scanner to process ESP32's 'Serial.println()' streams seamlessly
	scanner := bufio.NewScanner(port)
	for scanner.Scan() {
		// Output the ESP32 log instantly onto the terminal alongside BLE prompt
		fmt.Printf("[ESP32 MCU LOG] %s\n", scanner.Text())
	}

	if err := scanner.Err(); err != nil {
		log.Printf("[Serial Error] Serial stream reading interrupted: %v\n", err)
	}
}
