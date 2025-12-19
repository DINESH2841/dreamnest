// Dual-mode RFID: robust READ / WRITE with verification
// ESP8266 (NodeMCU), MFRC522
#include <SPI.h>
#include <MFRC522.h>

#define RST_PIN D3
#define SS_PIN  D4

MFRC522 mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;

int mode = 0; // 0 = idle, 1 = read, 2 = write
byte bufferLen = 18;
byte blockBuf[18];

void setup() {
  Serial.begin(115200);
  SPI.begin();
  mfrc522.PCD_Init();
  Serial.println("\n=== RFID MODE ===");
  Serial.println("Press 1 => READ mode");
  Serial.println("Press 2 => WRITE mode");
  Serial.println("=================\n");

  // default key A = FF FF FF FF FF FF (factory)
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;
}

void loop() {
  // choose mode from serial
  if (mode == 0) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '1') { mode = 1; Serial.println("Mode: READ (place card)"); }
      if (c == '2') { mode = 2; Serial.println("Mode: WRITE (place card)"); }
    }
    return;
  }

  // wait for a card
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  // print UID
  Serial.print("UID: ");
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) Serial.print("0");
    Serial.print(mfrc522.uid.uidByte[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  // card type
  MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
  Serial.print("Card Type: ");
  Serial.println(mfrc522.PICC_GetTypeName(piccType));

  // choose block list to try (avoid every 4th block = sector trailer)
  // We'll prefer block 2, then 4, then 5, etc.
  const int tryBlocks[] = {2,4,5,6,8,9,10,12,13,14};
  const int tryLen = sizeof(tryBlocks)/sizeof(tryBlocks[0]);

  // attempt auth/read/write depending on mode
  if (mode == 1) {
    // READ mode: try a block and print string printable bytes
    bool readOk = false;
    for (int b = 0; b < tryLen; b++) {
      int block = tryBlocks[b];
      if (tryReadBlock(block)) { readOk = true; break; }
    }
    if (!readOk) Serial.println("READ: No readable block found or auth failed.");
  } else if (mode == 2) {
    // WRITE mode: prompt user for text, then write to first writable block
    Serial.println("Enter text to write (max 16 chars). Then press Enter:");
    while (!Serial.available()) { delay(10); }
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() == 0) {
      Serial.println("Empty input. Cancel.");
      mode = 0; return;
    }
    if (input.length() > 16) input = input.substring(0,16);

    bool wrote = false;
    for (int b = 0; b < tryLen; b++) {
      int block = tryBlocks[b];
      if (tryWriteBlock(block, input)) { wrote = true; break; }
    }
    if (!wrote) Serial.println("WRITE: No writable block found or auth failed.");
  }

  // cleanup and reset mode
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  mode = 0;
}

// tries to authenticate & read a block; returns true if success and prints data
bool tryReadBlock(int block) {
  MFRC522::StatusCode status;
  Serial.print("Trying read block ");
  Serial.println(block);

  // try a few authentication retries (improves flaky hardware)
  const int MAX_TRIES = 3;
  for (int t = 0; t < MAX_TRIES; t++) {
    status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(mfrc522.uid));
    if (status == MFRC522::STATUS_OK) break;
    Serial.print(" Auth try "); Serial.print(t+1); Serial.print(" failed: ");
    Serial.println(mfrc522.GetStatusCodeName(status));
    delay(50);
  }
  if (status != MFRC522::STATUS_OK) {
    Serial.println(" Auth failed (read).");
    return false;
  }

  status = mfrc522.MIFARE_Read(block, blockBuf, &bufferLen);
  if (status != MFRC522::STATUS_OK) {
    Serial.print(" Read failed: ");
    Serial.println(mfrc522.GetStatusCodeName(status));
    return false;
  }

  // print printable characters
  String s = "";
  for (int i = 0; i < 16; i++) {
    char c = (char)blockBuf[i];
    if (isPrintable(c)) s += c;
  }
  s.trim();
  Serial.print("Read block "); Serial.print(block); Serial.print(": '");
  Serial.print(s);
  Serial.println("'");
  return true;
}

// tries to auth & write a block; returns true if success + verifies read-back
bool tryWriteBlock(int block, const String &text) {
  MFRC522::StatusCode status;
  Serial.print("Trying write block ");
  Serial.println(block);

  // authenticate with retries
  const int MAX_TRIES = 3;
  for (int t = 0; t < MAX_TRIES; t++) {
    status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(mfrc522.uid));
    if (status == MFRC522::STATUS_OK) break;
    Serial.print(" Auth try "); Serial.print(t+1); Serial.print(" failed: ");
    Serial.println(mfrc522.GetStatusCodeName(status));
    delay(50);
  }
  if (status != MFRC522::STATUS_OK) {
    Serial.println(" Auth failed (write).");
    return false;
  }

  // prepare 16-byte buffer
  byte data[16];
  for (int i = 0; i < 16; i++) data[i] = ' ';
  for (int i = 0; i < text.length() && i < 16; i++) data[i] = text[i];

  // write
  status = mfrc522.MIFARE_Write(block, data, 16);
  if (status != MFRC522::STATUS_OK) {
    Serial.print(" Write failed: ");
    Serial.println(mfrc522.GetStatusCodeName(status));
    return false;
  }
  Serial.println(" Write OK. Verifying read-back...");

  // read-back verification
  byte tmp[18];
  byte len = 18;
  status = mfrc522.MIFARE_Read(block, tmp, &len);
  if (status != MFRC522::STATUS_OK) {
    Serial.print(" Verify read failed: ");
    Serial.println(mfrc522.GetStatusCodeName(status));
    return false;
  }
  String s = "";
  for (int i = 0; i < 16; i++) if (isPrintable((char)tmp[i])) s += (char)tmp[i];
  s.trim();
  Serial.print(" Read-back: '"); Serial.print(s); Serial.println("'");

  if (s == text) {
    Serial.println(" VERIFY OK: data matches.");
    return true;
  } else {
    Serial.println(" VERIFY FAIL: data mismatch.");
    return false;
  }
}
