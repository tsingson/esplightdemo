package main

import (
	"fmt"
	"log"
	"os"
	"strings"
	"time"

	"github.com/tarm/serial"
)

func main() {
	// 1. Configure serial port parameters
	c := &serial.Config{
		Name:        "/dev/cu.usbserial-A5069RR4", // Your ESP32 port on macOS
		Baud:        115200,                       // Baud rate: 115200
		ReadTimeout: time.Millisecond * 500,       // Read timeout for waiting response
	}

	// 2. Open the serial port
	port, err := serial.OpenPort(c)
	if err != nil {
		log.Fatalf("Failed to open serial port: %v \nNote: Please check the port name and ensure it is not occupied by another program (e.g., Arduino Serial Monitor).", err)
	}
	defer port.Close()

	// 3. Parse command line arguments
	if len(os.Args) < 2 {
		fmt.Println("Usage: go run main.go [0|1|2|?]")
		fmt.Println("  0 - Mode 0 (Turn OFF LED)")
		fmt.Println("  1 - Mode 1 (Turn ON LED constantly)")
		fmt.Println("  2 - Mode 2 (Blink LED every 0.5s)")
		fmt.Println("  9 - Query current mode status")
		return
	}

	cmd := strings.TrimSpace(os.Args[1])

	// 4. Validate input and send data
	if cmd == "0" || cmd == "1" || cmd == "2" {
		_, err := port.Write([]byte(cmd))
		if err != nil {
			log.Fatalf("Failed to write to serial port: %v", err)
		}
		fmt.Printf("Command sent successfully -> Mode %s\n", cmd)

	} else if cmd == "9" {
		// Send query command
		_, err := port.Write([]byte("9"))
		if err != nil {
			log.Fatalf("Failed to write query to serial port: %v", err)
		}

		// Read response immediately
		buf := make([]byte, 1)
		n, err := port.Read(buf)
		if err != nil {
			log.Fatalf("Failed to read from serial port: %v", err)
		}

		if n > 0 {
			response := string(buf[:n])
			switch response {
			case "0":
				fmt.Println("ESP light off")
			case "1":
				fmt.Println("ESP light on")
			case "2":
				fmt.Println("ESP blink")
			default:
				fmt.Printf("Unknown status received: %s\n", response)
			}
		} else {
			fmt.Println("Error: No response received from ESP32 within timeout.")
		}

	} else {
		fmt.Println("Error: Invalid argument! Only 0, 1, 2, or 9 are allowed.")
	}
}
