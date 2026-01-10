# ESP32 WiFi Manager: Captive Portal with Modern UI & Multilingual Support

This is an advanced WiFi configuration manager for the ESP32, designed to provide a professional and seamless experience for the end user. It allows for easy network credential configuration through a modern web interface, eliminating the need to hardcode SSIDs and passwords.

## 🚀 Features

-   **Captive Portal:** Automatic redirection to the configuration page upon connecting to the access point.
-   **Modern UI:** Sleek, responsive design that works perfectly on both mobile and desktop devices.
-   **Multilingual Support:** Supports 5 languages (English, Spanish, Chinese, Portuguese, and French) with browser-side persistence.
-   **Network Scanning:** Automatically scans for nearby WiFi networks and displays them in a convenient dropdown menu.
-   **Security:** Features a password visibility toggle (eye icon) and secure credential storage using the `Preferences` library.
-   **Physical Factory Reset:** Dedicated reset mechanism (pin GPIO 4 to GND for 3 seconds) to wipe saved credentials.
-   **Status LED:** The built-in LED (Pin 2) provides real-time visual feedback on the connection status.

## 📸 Connection Process

Below is the configuration workflow:

| 1. Connect to AP | 2. WiFi Configuration | 3. Success |
| :---: | :---: | :---: |
| ![Step 1](./1.jpeg) | ![Step 2](./2.jpeg) | ![Step 3](./3.jpeg) |

## 🛠️ Hardware Requirements

-   **Microcontroller:** ESP32 (NodeMCU or similar).
-   **LED:** Connected to pin GPIO 2 (usually the built-in LED).
-   **Reset Button (Optional):** Connect pin GPIO 4 to Ground (GND) to trigger a factory reset.

## 💻 Installation

1.  Ensure you have the [Arduino IDE](https://www.arduino.cc/en/software) installed.
2.  Install the ESP32 board support in the IDE.
3.  Copy the code from `access-point.ino` into a new sketch.
4.  Connect your ESP32 and select the correct board under `Tools > Board`.
5.  Upload the code to your device.

## 📖 How to Use

1.  **Power On:** When the ESP32 starts, if no credentials are saved, the LED will be OFF and an open network named **"ESP32-Config"** will be created.
2.  **Connect:** Connect to the **"ESP32-Config"** WiFi network from your smartphone or PC.
3.  **Configure:** If the captive portal doesn't open automatically, navigate to `http://192.168.4.1` in your web browser.
4.  **Save:** Select your local network, enter the password, and click **"Save & Connect"**.
5.  **Reboot:** The ESP32 will restart and attempt to connect to your network. If successful, the built-in LED will turn ON.

## 🧹 Factory Reset

If you need to change networks or clear credentials:
1.  Connect pin **GPIO 4** to **GND** for at least 3 seconds.
2.  The LED will blink rapidly, indicating that the credentials have been cleared.
3.  The device will automatically restart in Configuration Mode.

---
Created with ❤️ for the Arduino and ESP32 community.
