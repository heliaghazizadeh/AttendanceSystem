#pragma config FOSC = XT
#pragma config WDTE = OFF
#pragma config PWRTE = ON
#pragma config BOREN = ON
#pragma config LVP = OFF
#pragma config CPD = OFF
#pragma config WRT = OFF
#pragma config CP = OFF

#define _XTAL_FREQ 4000000UL

#include <xc.h>
#include <stdint.h>
#include <string.h>

// ======================================================
// PIC16F877A Digital Attendance System with RC522 RFID
// Compiler: MPLAB X + XC8
// Clock: 4 MHz crystal
// Group Members: Shayan Kargar , Raya Ghazizadeh, Helia Ghazizadeh
// RC522 wiring:
//   RC522 SDA/SS  -> RC2 / pin 17
//   RC522 SCK     -> RC3 / pin 18
//   RC522 MOSI    -> RC5 / pin 24
//   RC522 MISO    -> RC4 / pin 23
//   RC522 RST     -> RE0 / pin 8
//   RC522 VCC     -> 3.3V only
//   RC522 GND     -> GND
//
// LCD wiring, 4-bit mode:
//   LCD RS -> RC0
//   LCD E  -> RC1
//   LCD RW -> GND
//   LCD D4 -> RD4
//   LCD D5 -> RD5
//   LCD D6 -> RD6
//   LCD D7 -> RD7
//
// Keypad:
//   Rows -> RB0-RB3
//   Cols -> RB4-RB7
//
// Outputs:
//   Green LED -> RA2
//   Red LED   -> RA3
//   Buzzer    -> RE1
//
// UART log:
//   TX -> RC6
//   RX -> RC7, optional/not used by RC522
// ======================================================

// ---------------- Project limits ----------------
#define MAX_STUDENTS 10
#define UID_SIZE     4
#define UID_TEXT_LEN 8
#define STUDENT_ID_LEN 8
#define ADMIN_PIN    "0000"
#define ADMIN_PIN_LEN 4

#define EEPROM_MAGIC_VALUE 0xAA
#define SESSION_NOT_MARKED 0x00
#define SESSION_MARKED     0x01
#define RECORD_EMPTY       0x00
#define RECORD_VALID       0xA5

#define KEYPAD_ROWS 4
#define KEYPAD_COLS 4
#define KEYPAD_DEBOUNCE_MS 20

#define CARD_REMOVAL_CLEAR_READS 5
#define CARD_REMOVAL_POLL_MS    100

#define BEEP_SHORT_MS           80
#define BEEP_MEDIUM_MS          150
#define BEEP_LONG_MS            180
#define BEEP_GAP_SHORT_MS       70
#define BEEP_GAP_MEDIUM_MS      100
#define ERROR_LED_MS            300
#define DUPLICATE_LED_MS        300
#define SUCCESS_LED_MS          300

#define STARTUP_MESSAGE_MS      2000
#define EEPROM_INIT_MESSAGE_MS  500
#define STATUS_HOLD_MS          250
#define ADMIN_COUNT_HOLD_MS     2000
#define ADMIN_RESET_HOLD_MS     1200
#define UART_IDLE_LOG_INTERVAL  50

// ---------------- EEPROM ----------------
#define EE_MAGIC   0x00
#define EE_COUNT   0x01
#define EE_RECORDS 0x10

#define STUDENT_RECORD_SIZE 14
#define REC_VALID_OFFSET    0
#define REC_ID_OFFSET       1
#define REC_UID_OFFSET      9
#define REC_MARKED_OFFSET   13

void EEPROM_Write(uint8_t addr, uint8_t data)
{
    EEADR = addr;
    EEDATA = data;
    EECON1bits.EEPGD = 0;
    EECON1bits.WREN = 1;
    INTCONbits.GIE = 0;
    EECON2 = 0x55;
    EECON2 = 0xAA;
    EECON1bits.WR = 1;
    while (EECON1bits.WR);
    EECON1bits.WREN = 0;
    INTCONbits.GIE = 1;
}

uint8_t EEPROM_Read(uint8_t addr)
{
    EEADR = addr;
    EECON1bits.EEPGD = 0;
    EECON1bits.RD = 1;
    return EEDATA;
}

void EEPROM_Init(void)
{
    uint8_t i;
    for (i = 0; i < MAX_STUDENTS; i++) {
        EEPROM_Write((uint8_t)(EE_RECORDS + (i * STUDENT_RECORD_SIZE) + REC_MARKED_OFFSET), SESSION_NOT_MARKED);
    }
    EEPROM_Write(EE_COUNT, SESSION_NOT_MARKED);
    EEPROM_Write(EE_MAGIC, EEPROM_MAGIC_VALUE);
}

uint8_t Session_IsMarked(uint8_t studentIndex)
{
    return (EEPROM_Read((uint8_t)(EE_RECORDS + (studentIndex * STUDENT_RECORD_SIZE) + REC_MARKED_OFFSET)) == SESSION_MARKED);
}

uint8_t Session_MarkPresent(uint8_t studentIndex)
{
    uint8_t count;

    EEPROM_Write((uint8_t)(EE_RECORDS + (studentIndex * STUDENT_RECORD_SIZE) + REC_MARKED_OFFSET), SESSION_MARKED);
    count = EEPROM_Read(EE_COUNT);
    if (count < MAX_STUDENTS) count++;
    EEPROM_Write(EE_COUNT, count);

    return count;
}

uint8_t Student_RecordAddr(uint8_t studentIndex)
{
    return (uint8_t)(EE_RECORDS + (studentIndex * STUDENT_RECORD_SIZE));
}

uint8_t Student_IsValid(uint8_t studentIndex)
{
    return (EEPROM_Read((uint8_t)(Student_RecordAddr(studentIndex) + REC_VALID_OFFSET)) == RECORD_VALID);
}

void Student_ReadID(uint8_t studentIndex, char *studentID)
{
    uint8_t i;
    uint8_t base = Student_RecordAddr(studentIndex);

    for (i = 0; i < STUDENT_ID_LEN; i++) {
        studentID[i] = (char)EEPROM_Read((uint8_t)(base + REC_ID_OFFSET + i));
    }
    studentID[STUDENT_ID_LEN] = '\0';
}

void Student_WriteID(uint8_t studentIndex, const char *studentID)
{
    uint8_t i;
    uint8_t base = Student_RecordAddr(studentIndex);

    for (i = 0; i < STUDENT_ID_LEN; i++) {
        EEPROM_Write((uint8_t)(base + REC_ID_OFFSET + i), (uint8_t)studentID[i]);
    }
}

void Student_ReadUID(uint8_t studentIndex, uint8_t *uid)
{
    uint8_t i;
    uint8_t base = Student_RecordAddr(studentIndex);

    for (i = 0; i < UID_SIZE; i++) {
        uid[i] = EEPROM_Read((uint8_t)(base + REC_UID_OFFSET + i));
    }
}

void Student_WriteUID(uint8_t studentIndex, const uint8_t *uid)
{
    uint8_t i;
    uint8_t base = Student_RecordAddr(studentIndex);

    for (i = 0; i < UID_SIZE; i++) {
        EEPROM_Write((uint8_t)(base + REC_UID_OFFSET + i), uid[i]);
    }
}

void Students_ClearAll(void)
{
    uint8_t i;
    uint8_t j;
    uint8_t base;

    for (i = 0; i < MAX_STUDENTS; i++) {
        base = Student_RecordAddr(i);
        for (j = 0; j < STUDENT_RECORD_SIZE; j++) {
            EEPROM_Write((uint8_t)(base + j), 0x00);
        }
    }
    EEPROM_Write(EE_COUNT, 0x00);
    EEPROM_Write(EE_MAGIC, EEPROM_MAGIC_VALUE);
}

uint8_t UID_Equals(const uint8_t *a, const uint8_t *b)
{
    uint8_t i;
    for (i = 0; i < UID_SIZE; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

int8_t FindStudentByUID(const uint8_t *uid)
{
    uint8_t i;
    uint8_t savedUID[UID_SIZE];

    for (i = 0; i < MAX_STUDENTS; i++) {
        if (Student_IsValid(i)) {
            Student_ReadUID(i, savedUID);
            if (UID_Equals(uid, savedUID)) return (int8_t)i;
        }
    }
    return -1;
}

int8_t FindStudentByID(const char *studentID)
{
    uint8_t i;
    char savedID[STUDENT_ID_LEN + 1];

    for (i = 0; i < MAX_STUDENTS; i++) {
        if (Student_IsValid(i)) {
            Student_ReadID(i, savedID);
            if (strcmp(studentID, savedID) == 0) return (int8_t)i;
        }
    }
    return -1;
}

int8_t FindEmptyStudentSlot(void)
{
    uint8_t i;

    for (i = 0; i < MAX_STUDENTS; i++) {
        if (!Student_IsValid(i)) return (int8_t)i;
    }
    return -1;
}

uint8_t Students_CountEnrolled(void)
{
    uint8_t i;
    uint8_t count = 0;

    for (i = 0; i < MAX_STUDENTS; i++) {
        if (Student_IsValid(i)) count++;
    }
    return count;
}

void Student_Save(uint8_t studentIndex, const char *studentID, const uint8_t *uid)
{
    uint8_t base = Student_RecordAddr(studentIndex);

    Student_WriteID(studentIndex, studentID);
    Student_WriteUID(studentIndex, uid);
    EEPROM_Write((uint8_t)(base + REC_MARKED_OFFSET), SESSION_NOT_MARKED);
    EEPROM_Write((uint8_t)(base + REC_VALID_OFFSET), RECORD_VALID);
}

// ---------------- LCD ----------------
#define LCD_RS          PORTCbits.RC0
#define LCD_EN          PORTCbits.RC1
#define LCD_SET_DATA(n) do { PORTD = (PORTD & 0x0F) | (((n) & 0x0F) << 4); } while (0)

void LCD_Pulse(void)
{
    LCD_EN = 1;
    __delay_us(2);
    LCD_EN = 0;
    __delay_us(50);
}

void LCD_Nibble(uint8_t n)
{
    LCD_SET_DATA(n);
    LCD_Pulse();
}

void LCD_Send(uint8_t b, uint8_t rs)
{
    LCD_RS = rs;
    LCD_Nibble((uint8_t)(b >> 4));
    LCD_Nibble((uint8_t)(b & 0x0F));
    __delay_us(50);
}

void LCD_Cmd(uint8_t c) { LCD_Send(c, 0); }
void LCD_Char(uint8_t c) { LCD_Send(c, 1); }

void LCD_Init(void)
{
    __delay_ms(20);
    LCD_RS = 0;
    LCD_EN = 0;
    LCD_Nibble(0x03);
    __delay_ms(5);
    LCD_Nibble(0x03);
    __delay_us(150);
    LCD_Nibble(0x03);
    __delay_us(150);
    LCD_Nibble(0x02);
    __delay_us(150);
    LCD_Cmd(0x28);
    LCD_Cmd(0x0C);
    LCD_Cmd(0x06);
    LCD_Cmd(0x01);
    __delay_ms(2);
}

void LCD_Clear(void)
{
    LCD_Cmd(0x01);
    __delay_ms(2);
}

void LCD_Goto(uint8_t row, uint8_t col)
{
    LCD_Cmd((uint8_t)((row == 0 ? 0x80 : 0xC0) + col));
}

void LCD_Print(const char *s)
{
    while (*s) LCD_Char((uint8_t)*s++);
}

void LCD_Num(uint8_t n)
{
    if (n >= 100) LCD_Char((uint8_t)('0' + n / 100));
    if (n >= 10) LCD_Char((uint8_t)('0' + (n / 10) % 10));
    LCD_Char((uint8_t)('0' + n % 10));
}

void LCD_ShowIdle(void)
{
    LCD_Clear();
    LCD_Goto(0, 0);
    LCD_Print("Tap RFID Card");
    LCD_Goto(1, 0);
    LCD_Print("*=Admin");
}

void LCD_ShowIDStatus(const char *line1, const char *studentID)
{
    LCD_Clear();
    LCD_Goto(0, 0);
    LCD_Print(line1);
    LCD_Goto(1, 0);
    LCD_Print("ID:");
    LCD_Print(studentID);
}

// ---------------- UART log ----------------
void UART_Init(void)
{
    TRISCbits.TRISC6 = 0;
    TRISCbits.TRISC7 = 1;
    SPBRG = 25;               // 9600 baud at 4 MHz, BRGH=1
    TXSTAbits.BRGH = 1;
    TXSTAbits.TXEN = 1;
    TXSTAbits.SYNC = 0;
    RCSTAbits.SPEN = 1;
    RCSTAbits.CREN = 1;
}

void UART_SendChar(char c)
{
    while (!TXSTAbits.TRMT);
    TXREG = c;
}

void UART_Print(const char *s)
{
    while (*s) UART_SendChar(*s++);
}

void UART_Println(const char *s)
{
    UART_Print(s);
    UART_SendChar('\r');
    UART_SendChar('\n');
}

void UART_Num(uint8_t n)
{
    char buf[4];
    uint8_t i = 0;
    if (n == 0) {
        UART_SendChar('0');
        return;
    }
    while (n > 0) {
        buf[i++] = (char)('0' + (n % 10));
        n = (uint8_t)(n / 10);
    }
    while (i--) UART_SendChar(buf[i]);
}

// ---------------- Buzzer and LEDs ----------------
#define GREEN_LED PORTAbits.RA2
#define RED_LED   PORTAbits.RA3
#define BUZZER    PORTEbits.RE1

void Beep(uint16_t ms)
{
    uint16_t i;
    BUZZER = 1;
    for (i = 0; i < ms; i++) __delay_ms(1);
    BUZZER = 0;
}

void Sound_Success(void)
{
    Beep(BEEP_SHORT_MS);
}

void Sound_Duplicate(void)
{
    Beep(BEEP_MEDIUM_MS);
    __delay_ms(BEEP_GAP_MEDIUM_MS);
    Beep(BEEP_MEDIUM_MS);
}

void Sound_UnknownCard(void)
{
    Beep(BEEP_LONG_MS);
}

void Sound_WrongPin(void)
{
    Beep(BEEP_SHORT_MS);
    __delay_ms(BEEP_GAP_SHORT_MS);
    Beep(BEEP_SHORT_MS);
    __delay_ms(BEEP_GAP_SHORT_MS);
    Beep(BEEP_SHORT_MS);
}

void Sound_AdminSaved(void)
{
    Beep(BEEP_SHORT_MS);
    __delay_ms(BEEP_GAP_SHORT_MS);
    Beep(BEEP_SHORT_MS);
}

void Sound_AdminReset(void)
{
    Beep(BEEP_MEDIUM_MS);
}

void GreenOn(uint16_t ms)
{
    uint16_t i;
    GREEN_LED = 1;
    for (i = 0; i < ms; i++) __delay_ms(1);
    GREEN_LED = 0;
}

void RedOn(uint16_t ms)
{
    uint16_t i;
    RED_LED = 1;
    for (i = 0; i < ms; i++) __delay_ms(1);
    RED_LED = 0;
}

// ---------------- Keypad, admin only ----------------
const char KEY_MAP[KEYPAD_ROWS][KEYPAD_COLS] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};

char Keypad_Scan(void)
{
    uint8_t r, c;
    for (r = 0; r < KEYPAD_ROWS; r++) {
        PORTB = (uint8_t)((PORTB | 0x0F) & ~(1u << r));
        __delay_us(10);
        uint8_t cols = (uint8_t)((PORTB >> 4) & 0x0F);
        if (cols != 0x0F) {
            __delay_ms(KEYPAD_DEBOUNCE_MS);
            cols = (uint8_t)((PORTB >> 4) & 0x0F);
            for (c = 0; c < KEYPAD_COLS; c++) {
                if (!(cols & (1u << c))) {
                    while (!((PORTB >> 4) & (1u << c)));
                    __delay_ms(KEYPAD_DEBOUNCE_MS);
                    return KEY_MAP[r][c];
                }
            }
        }
    }
    return 0;
}

// ---------------- RC522 / MFRC522 ----------------
#define RC522_CS     PORTCbits.RC2
#define RC522_RST    PORTEbits.RE0

#define PCD_IDLE       0x00
#define PCD_AUTHENT    0x0E
#define PCD_RECEIVE    0x08
#define PCD_TRANSMIT   0x04
#define PCD_TRANSCEIVE 0x0C
#define PCD_RESETPHASE 0x0F
#define PCD_CALCCRC    0x03

#define PICC_REQIDL    0x26
#define PICC_ANTICOLL  0x93

#define MI_OK          0
#define MI_NOTAGERR    1
#define MI_ERR         2

#define CommandReg      0x01
#define ComIEnReg       0x02
#define CommIrqReg      0x04
#define DivIrqReg       0x05
#define ErrorReg        0x06
#define Status2Reg      0x08
#define FIFODataReg     0x09
#define FIFOLevelReg    0x0A
#define ControlReg      0x0C
#define BitFramingReg   0x0D
#define ModeReg         0x11
#define TxControlReg    0x14
#define TxASKReg        0x15
#define CRCResultRegM   0x21
#define CRCResultRegL   0x22
#define TModeReg        0x2A
#define TPrescalerReg   0x2B
#define TReloadRegH     0x2C
#define TReloadRegL     0x2D

uint8_t SPI_Transfer(uint8_t data)
{
    SSPBUF = data;
    while (!SSPSTATbits.BF);
    return SSPBUF;
}

void SPI_Init(void)
{
    TRISCbits.TRISC3 = 0;     // SCK
    TRISCbits.TRISC4 = 1;     // SDI / MISO
    TRISCbits.TRISC5 = 0;     // SDO / MOSI
    SSPSTATbits.SMP = 0;
    SSPSTATbits.CKE = 1;
    SSPCONbits.SSPM = 0b0010; // SPI master, Fosc/64
    SSPCONbits.CKP = 0;
    SSPCONbits.SSPEN = 1;
}

void RC522_WriteReg(uint8_t addr, uint8_t val)
{
    RC522_CS = 0;
    SPI_Transfer((uint8_t)((addr << 1) & 0x7E));
    SPI_Transfer(val);
    RC522_CS = 1;
}

uint8_t RC522_ReadReg(uint8_t addr)
{
    uint8_t val;
    RC522_CS = 0;
    SPI_Transfer((uint8_t)(((addr << 1) & 0x7E) | 0x80));
    val = SPI_Transfer(0x00);
    RC522_CS = 1;
    return val;
}

void RC522_SetBitMask(uint8_t reg, uint8_t mask)
{
    RC522_WriteReg(reg, (uint8_t)(RC522_ReadReg(reg) | mask));
}

void RC522_ClearBitMask(uint8_t reg, uint8_t mask)
{
    RC522_WriteReg(reg, (uint8_t)(RC522_ReadReg(reg) & ~mask));
}

void RC522_AntennaOn(void)
{
    uint8_t temp = RC522_ReadReg(TxControlReg);
    if (!(temp & 0x03)) RC522_SetBitMask(TxControlReg, 0x03);
}

void RC522_Reset(void)
{
    RC522_RST = 1;
    __delay_ms(5);
    RC522_WriteReg(CommandReg, PCD_RESETPHASE);
    __delay_ms(50);
}

void RC522_Init(void)
{
    RC522_CS = 1;
    RC522_RST = 1;
    __delay_ms(50);
    RC522_Reset();
    RC522_WriteReg(TModeReg, 0x8D);
    RC522_WriteReg(TPrescalerReg, 0x3E);
    RC522_WriteReg(TReloadRegL, 30);
    RC522_WriteReg(TReloadRegH, 0);
    RC522_WriteReg(TxASKReg, 0x40);
    RC522_WriteReg(ModeReg, 0x3D);
    RC522_AntennaOn();
}

uint8_t RC522_ToCard(uint8_t command, uint8_t *sendData, uint8_t sendLen,
                     uint8_t *backData, uint16_t *backLen)
{
    uint8_t status = MI_ERR;
    uint8_t irqEn = 0x00;
    uint8_t waitIRq = 0x00;
    uint8_t lastBits;
    uint8_t n;
    uint16_t i;

    if (command == PCD_AUTHENT) {
        irqEn = 0x12;
        waitIRq = 0x10;
    } else if (command == PCD_TRANSCEIVE) {
        irqEn = 0x77;
        waitIRq = 0x30;
    }

    RC522_WriteReg(ComIEnReg, (uint8_t)(irqEn | 0x80));
    RC522_ClearBitMask(CommIrqReg, 0x80);
    RC522_SetBitMask(FIFOLevelReg, 0x80);
    RC522_WriteReg(CommandReg, PCD_IDLE);

    for (i = 0; i < sendLen; i++) RC522_WriteReg(FIFODataReg, sendData[i]);

    RC522_WriteReg(CommandReg, command);
    if (command == PCD_TRANSCEIVE) RC522_SetBitMask(BitFramingReg, 0x80);

    i = 2000;
    do {
        n = RC522_ReadReg(CommIrqReg);
        i--;
    } while ((i != 0) && !(n & 0x01) && !(n & waitIRq));

    RC522_ClearBitMask(BitFramingReg, 0x80);

    if (i != 0) {
        if (!(RC522_ReadReg(ErrorReg) & 0x1B)) {
            status = MI_OK;
            if (n & irqEn & 0x01) status = MI_NOTAGERR;

            if (command == PCD_TRANSCEIVE) {
                n = RC522_ReadReg(FIFOLevelReg);
                lastBits = (uint8_t)(RC522_ReadReg(ControlReg) & 0x07);
                if (lastBits) *backLen = (uint16_t)((n - 1) * 8 + lastBits);
                else *backLen = (uint16_t)(n * 8);
                if (n == 0) n = 1;
                if (n > 16) n = 16;
                for (i = 0; i < n; i++) backData[i] = RC522_ReadReg(FIFODataReg);
            }
        } else {
            status = MI_ERR;
        }
    }

    return status;
}

uint8_t RC522_Request(uint8_t reqMode, uint8_t *tagType)
{
    uint8_t status;
    uint16_t backBits;

    RC522_WriteReg(BitFramingReg, 0x07);
    tagType[0] = reqMode;
    status = RC522_ToCard(PCD_TRANSCEIVE, tagType, 1, tagType, &backBits);
    if ((status != MI_OK) || (backBits != 0x10)) status = MI_ERR;
    return status;
}

uint8_t RC522_AntiColl(uint8_t *serNum)
{
    uint8_t status;
    uint8_t i;
    uint8_t serNumCheck = 0;
    uint16_t unLen;

    RC522_WriteReg(BitFramingReg, 0x00);
    serNum[0] = PICC_ANTICOLL;
    serNum[1] = 0x20;
    status = RC522_ToCard(PCD_TRANSCEIVE, serNum, 2, serNum, &unLen);

    if (status == MI_OK) {
        for (i = 0; i < UID_SIZE; i++) serNumCheck ^= serNum[i];
        if (serNumCheck != serNum[4]) status = MI_ERR;
    }
    return status;
}

uint8_t RC522_ReadUID(uint8_t *uid)
{
    uint8_t tagType[2];
    uint8_t serNum[5];
    uint8_t i;

    if (RC522_Request(PICC_REQIDL, tagType) != MI_OK) return 0;
    if (RC522_AntiColl(serNum) != MI_OK) return 0;
    for (i = 0; i < UID_SIZE; i++) uid[i] = serNum[i];
    return 1;
}

void RC522_WaitForCardRemoval(void)
{
    uint8_t tagType[2];
    uint8_t clearReads = 0;

    while (clearReads < CARD_REMOVAL_CLEAR_READS) {
        if (RC522_Request(PICC_REQIDL, tagType) == MI_OK) {
            clearReads = 0;
        } else {
            clearReads++;
        }
        __delay_ms(CARD_REMOVAL_POLL_MS);
    }
}

char HexNibble(uint8_t v)
{
    v = (uint8_t)(v & 0x0F);
    return (char)(v < 10 ? ('0' + v) : ('A' + v - 10));
}

void UID_ToString(uint8_t *uid, char *out)
{
    uint8_t i;
    for (i = 0; i < UID_SIZE; i++) {
        out[i * 2] = HexNibble((uint8_t)(uid[i] >> 4));
        out[(i * 2) + 1] = HexNibble(uid[i]);
    }
    out[UID_TEXT_LEN] = '\0';
}

// ---------------- Dynamic student enrollment ----------------
uint8_t Keypad_ReadStudentID(char *studentID)
{
    uint8_t idx = 0;
    char key;

    LCD_Clear();
    LCD_Goto(0, 0);
    LCD_Print("Enter StudentID");
    LCD_Goto(1, 0);

    while (1) {
        key = 0;
        while (!key) key = Keypad_Scan();

        if (key >= '0' && key <= '9') {
            if (idx < STUDENT_ID_LEN) {
                studentID[idx++] = key;
                LCD_Char((uint8_t)key);
            }
        } else if (key == '#') {
            if (idx > 0) break;
        } else if (key == '*') {
            studentID[0] = '\0';
            return 0;
        }
    }

    while (idx < STUDENT_ID_LEN) studentID[idx++] = '\0';
    studentID[STUDENT_ID_LEN] = '\0';
    return 1;
}

void Admin_EnrollStudent(void)
{
    char studentID[STUDENT_ID_LEN + 1];
    uint8_t uid[UID_SIZE];
    char uidText[UID_TEXT_LEN + 1];
    int8_t idSlot;
    int8_t uidSlot;
    int8_t targetSlot;

    if (!Keypad_ReadStudentID(studentID)) {
        LCD_Clear();
        LCD_Goto(0, 0);
        LCD_Print("ENROLL CANCEL");
        __delay_ms(STATUS_HOLD_MS);
        return;
    }

    LCD_Clear();
    LCD_Goto(0, 0);
    LCD_Print("Tap RFID Card");
    LCD_Goto(1, 0);
    LCD_Print(studentID);

    while (!RC522_ReadUID(uid));
    UID_ToString(uid, uidText);

    idSlot = FindStudentByID(studentID);
    uidSlot = FindStudentByUID(uid);

    if ((uidSlot >= 0) && (uidSlot != idSlot)) {
        LCD_Clear();
        LCD_Goto(0, 0);
        LCD_Print("CARD EXISTS");
        LCD_Goto(1, 0);
        LCD_Print(uidText);
        UART_Print("[ENROLL] Card already assigned UID:");
        UART_Println(uidText);
        Sound_UnknownCard();
        RedOn(ERROR_LED_MS);
        __delay_ms(STATUS_HOLD_MS);
        RC522_WaitForCardRemoval();
        return;
    }

    if (idSlot >= 0) {
        targetSlot = idSlot;
    } else {
        targetSlot = FindEmptyStudentSlot();
    }

    if (targetSlot < 0) {
        LCD_Clear();
        LCD_Goto(0, 0);
        LCD_Print("MEMORY FULL");
        UART_Println("[ENROLL] Memory full");
        Sound_UnknownCard();
        RedOn(ERROR_LED_MS);
        __delay_ms(STATUS_HOLD_MS);
        RC522_WaitForCardRemoval();
        return;
    }

    Student_Save((uint8_t)targetSlot, studentID, uid);

    LCD_Clear();
    LCD_Goto(0, 0);
    LCD_Print(idSlot >= 0 ? "ID UPDATED" : "ID SAVED");
    LCD_Goto(1, 0);
    LCD_Print(studentID);
    UART_Print("[ENROLL] ID:");
    UART_Print(studentID);
    UART_Print(" UID:");
    UART_Print(uidText);
    UART_Println(idSlot >= 0 ? " UPDATED" : " SAVED");
    Sound_AdminSaved();
    GreenOn(SUCCESS_LED_MS);
    RC522_WaitForCardRemoval();
}

// ---------------- Admin mode ----------------
void Admin_Mode(void)
{
    char pin[ADMIN_PIN_LEN + 1] = {0};
    uint8_t idx = 0;
    char key;

    LCD_Clear();
    LCD_Goto(0, 0);
    LCD_Print("ADMIN: Enter PIN");
    LCD_Goto(1, 0);
    LCD_Print("PIN: ");

    while (idx < ADMIN_PIN_LEN) {
        key = 0;
        while (!key) key = Keypad_Scan();
        if (key >= '0' && key <= '9') {
            pin[idx++] = key;
            LCD_Char('*');
        } else if (key == '#') {
            break;
        }
    }
    pin[ADMIN_PIN_LEN] = '\0';

    if (strcmp(pin, ADMIN_PIN) != 0) {
        LCD_Clear();
        LCD_Goto(0, 0);
        LCD_Print("WRONG PASSWORD");
        UART_Println("[ADMIN] Wrong password");
        Sound_WrongPin();
        RedOn(ERROR_LED_MS);
        return;
    }

    UART_Println("[ADMIN] Admin mode entered");

    uint8_t stay = 1;
    while (stay) {
        LCD_Clear();
        LCD_Goto(0, 0);
        LCD_Print("1:Cnt 2:Reset");
        LCD_Goto(1, 0);
        LCD_Print("3:En 4:IDs #:Ex");

        key = 0;
        while (!key) key = Keypad_Scan();

        switch (key) {
            case '1': {
                uint8_t cnt = EEPROM_Read(EE_COUNT);
                uint8_t enrolled = Students_CountEnrolled();
                LCD_Clear();
                LCD_Goto(0, 0);
                LCD_Print("Present: ");
                LCD_Num(cnt);
                LCD_Goto(1, 0);
                LCD_Print("Enrolled: ");
                LCD_Num(enrolled);
                UART_Print("[ADMIN] Present: ");
                UART_Num(cnt);
                UART_Print(" Enrolled: ");
                UART_Num(enrolled);
                UART_Print(" Capacity: ");
                UART_Num(MAX_STUDENTS);
                UART_Println("");
                __delay_ms(ADMIN_COUNT_HOLD_MS);
                break;
            }
            case '2':
                LCD_Clear();
                LCD_Goto(0, 0);
                LCD_Print("RESET ATTEND...");
                EEPROM_Init();
                LCD_Clear();
                LCD_Goto(0, 0);
                LCD_Print("ATTEND RESET");
                UART_Println("[ADMIN] Session cleared");
                Sound_AdminReset();
                __delay_ms(ADMIN_RESET_HOLD_MS);
                break;
            case '3':
                Admin_EnrollStudent();
                break;
            case '4':
                LCD_Clear();
                LCD_Goto(0, 0);
                LCD_Print("CLEAR STUDENTS");
                LCD_Goto(1, 0);
                LCD_Print("Press # confirm");
                key = 0;
                while (!key) key = Keypad_Scan();
                if (key == '#') {
                    Students_ClearAll();
                    LCD_Clear();
                    LCD_Goto(0, 0);
                    LCD_Print("STUDENTS CLEAR");
                    UART_Println("[ADMIN] Students cleared");
                    Sound_AdminReset();
                    __delay_ms(ADMIN_RESET_HOLD_MS);
                }
                break;
            case '#':
                stay = 0;
                break;
            default:
                break;
        }
    }

    UART_Println("[ADMIN] Exited admin mode");
}

// ---------------- Main ----------------
static uint8_t log_count = 0;

void main(void)
{
    uint8_t uid[UID_SIZE];
    char uidText[UID_TEXT_LEN + 1];
    char studentID[STUDENT_ID_LEN + 1];
    int8_t sidx;
    uint8_t cnt;
    uint8_t idleTicks;
    char k;

    ADCON1 = 0x06;            // Make PORTA/PORTE digital I/O

    TRISAbits.TRISA2 = 0;
    TRISAbits.TRISA3 = 0;
    TRISB = 0xF0;
    TRISC = 0x90;             // RC7 input, RC4 input; others outputs before SPI/UART init
    TRISD = 0x00;
    TRISEbits.TRISE0 = 0;
    TRISEbits.TRISE1 = 0;

    PORTA = 0x00;
    PORTB = 0x0F;
    PORTC = 0x00;
    PORTD = 0x00;
    PORTE = 0x00;

    OPTION_REGbits.nRBPU = 0; // Enable PORTB weak pull-ups

    LCD_Init();
    UART_Init();
    SPI_Init();
    RC522_Init();

    if (EEPROM_Read(EE_MAGIC) != EEPROM_MAGIC_VALUE) {
        LCD_Clear();
        LCD_Goto(0, 0);
        LCD_Print("INIT EEPROM...");
        Students_ClearAll();
        __delay_ms(EEPROM_INIT_MESSAGE_MS);
    }

    LCD_Clear();
    LCD_Goto(0, 0);
    LCD_Print("ATTENDANCE SYS");
    LCD_Goto(1, 0);
    LCD_Print("RC522 READY");
    UART_Println("=== ATTENDANCE SYSTEM RC522 STARTED ===");
    UART_Println("[UART] 9600 8N1 log test");
    UART_Println("[UART] If you see this, USB-TTL RX is working");
    __delay_ms(STARTUP_MESSAGE_MS);

    while (1) {
        LCD_ShowIdle();
        idleTicks = 0;

        while (!RC522_ReadUID(uid)) {
            k = Keypad_Scan();
            if (k == '*') {
                Admin_Mode();
                LCD_ShowIdle();
                idleTicks = 0;
            }
            __delay_ms(CARD_REMOVAL_POLL_MS);
            idleTicks++;
            if (idleTicks >= UART_IDLE_LOG_INTERVAL) {
                UART_Print("[IDLE] Enrolled:");
                UART_Num(Students_CountEnrolled());
                UART_Print(" Present:");
                UART_Num(EEPROM_Read(EE_COUNT));
                UART_Println(" Waiting for card");
                idleTicks = 0;
            }
        }

        UID_ToString(uid, uidText);
        sidx = FindStudentByUID(uid);
        log_count++;

        UART_Print("[");
        UART_Num(log_count);
        UART_Print("] UID:");
        UART_Print(uidText);

        if (sidx < 0) {
            LCD_Clear();
            LCD_Goto(0, 0);
            LCD_Print("UNKNOWN CARD");
            LCD_Goto(1, 0);
            LCD_Print("Try again");
            UART_Println(" -- UNKNOWN");
            Sound_UnknownCard();
            RedOn(ERROR_LED_MS);
            __delay_ms(STATUS_HOLD_MS);
            RC522_WaitForCardRemoval();
            continue;
        }

        if (Session_IsMarked((uint8_t)sidx)) {
            Student_ReadID((uint8_t)sidx, studentID);
            LCD_ShowIDStatus("DUPLICATE", studentID);
            UART_Print(" -- DUPLICATE ID:");
            UART_Println(studentID);
            Sound_Duplicate();
            RedOn(DUPLICATE_LED_MS);
            __delay_ms(STATUS_HOLD_MS);
            RC522_WaitForCardRemoval();
            continue;
        }

        cnt = Session_MarkPresent((uint8_t)sidx);
        Student_ReadID((uint8_t)sidx, studentID);

        LCD_ShowIDStatus("PRESENT", studentID);
        UART_Print(" -- PRESENT ID:");
        UART_Print(studentID);
        UART_Print(" Total:");
        UART_Num(cnt);
        UART_Println("");
        Sound_Success();
        GreenOn(SUCCESS_LED_MS);

        RC522_WaitForCardRemoval();
    }
}
