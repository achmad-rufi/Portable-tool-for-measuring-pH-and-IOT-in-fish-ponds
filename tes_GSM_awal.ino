/*
 * PROGRAM AUTO-MONITOR DATA ESP32 + SIM800L
 * Fungsi: Secara otomatis meminta data dari SIM800L dan
 * menampilkannya di Serial Monitor komputer.
 */

// Definisi Pin (Hardware Serial 2)
#define RXD2 26 // Hubungkan ke TX SIM800L
#define TXD2 27 // Hubungkan ke RX SIM800L
#define BAUDRATE_SIM 9600 

unsigned long previousMillis = 0;
const long interval = 5000; // Ambil data setiap 5000ms (5 detik)

void setup() {
  // 1. Serial Monitor (Komputer)
  Serial.begin(9600);
  
  // 2. Serial SIM800L
  Serial2.begin(BAUDRATE_SIM, SERIAL_8N1, RXD2, TXD2);

  delay(2000);
  Serial.println("\n\n=== SISTEM MONITOR DATA SIM800L ===");
  Serial.println("Menunggu respon awal dari modem...");
  
  // Kirim perintah AT dasar untuk memastikan modem hidup
  sendATCommand("AT"); 
}

void loop() {
  unsigned long currentMillis = millis();

  // Setiap 5 detik, jalankan perintah pengecekan
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    Serial.println("\n--------------------------------");
    Serial.println("[ MEMBACA DATA BARU ]");
    
    // 1. Cek Kualitas Sinyal
    // Respon normal: +CSQ: 20,0 (Angka pertama 0-31. Di bawah 10 = Jelek/Putus)
    sendATCommand("AT+CSQ"); 

    // 2. Cek Nama Operator / Provider
    // Respon normal: +COPS: 0,0,"Telkomsel"
    sendATCommand("AT+COPS?"); 

    // 3. Cek Status Registrasi Jaringan
    // Respon normal: +CREG: 0,1 (Home) atau 0,5 (Roaming). 
    // Jika 0,2 artinya masih mencari (searching).
    sendATCommand("AT+CREG?");

    // 4. Cek Tegangan Supply (Opsional, beberapa modul support ini)
    // Berguna untuk tahu apakah power supply drop
    sendATCommand("AT+CBC"); 

    Serial.println("--------------------------------");
  }

  // Tetap izinkan mode manual jika Anda ingin mengetik sesuatu
  if (Serial.available()) {
    Serial2.write(Serial.read());
  }
  if (Serial2.available()) {
    Serial.write(Serial2.read());
  }
}

// --- FUNGSI KHUSUS UNTUK MENGIRIM DAN MENAMPILKAN DATA ---
void sendATCommand(String command) {
  // 1. Tampilkan perintah yang dikirim di Serial Monitor
  Serial.print("MENGIRIM >> ");
  Serial.println(command);

  // 2. Kirim perintah ke SIM800L
  Serial2.println(command);

  // 3. Tunggu sebentar agar SIM800L sempat memproses
  delay(500);

  // 4. Baca semua balasan dari SIM800L dan tampilkan
  Serial.print("DITERIMA << ");
  while (Serial2.available()) {
    String response = Serial2.readString();
    // Hilangkan spasi kosong berlebih agar rapi
    response.trim(); 
    Serial.println(response);
  }
}