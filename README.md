# TCP Connection Aggregator Proxy

A simple tool that allows multiple connections to a Modbus TCP device.

## Overview

This tool is designed to facilitate multiple Modbus masters to collect data from a Modbus device (e.g., a Huawei PV inverter) that supports only a single TCP connection at a time. The device's behavior is such that when a new connection is opened, the existing connection is dropped. This tool solves that problem by acting as an intermediary.

## Behavior

- Connects to a Modbus TCP device.
- Opens a TCP listening socket to accept multiple incoming connections.
- Forwards incoming data from each connection to the Modbus TCP device.
- Waits up to 5 seconds for a reply from the device and forwards it back to the respective connection.
- Note: the tool is not aware of the Modbus protocol; it simply handles TCP connections and traffic.

## Usage

1. Run the tool to start the TCP listening socket.
2. Connect your Modbus masters to the tool's listening socket.
3. The tool will manage the connections and data forwarding automatically.

## Example 

```sh
sudo ./tcp-connection-aggregator-proxy -s 192.168.1.50 -p 502 -l 502
```
This command starts the proxy, connecting to the Modbus device at `192.168.1.50` on the standard Modbus port `502`, and listens for incoming TCP connections on local port `502` (root privileges required). This setup allows multiple Modbus masters to connect and communicate with the PV inverter seamlessly.

### Command Breakdown
- `-s 192.168.1.50`: Specifies the IP address of the Modbus device.
- `-p 502`: Specifies the port of the Modbus device.
- `-l 502`: Specifies the local port for incoming connections.

Ensure you have the necessary permissions to run the command with `sudo`.

## License

This project is licensed under the GPLv3 License.
