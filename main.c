#include <xc.h>
#include <stdio.h>

// --- FUSES (20MHz) ---
#pragma config FOSC = HS
#pragma config CPUDIV = OSC1_PLL2 
#pragma config WDT = OFF
#pragma config LVP = OFF
#pragma config PBADEN = OFF

#define _XTAL_FREQ 20000000

// ==========================================
//   LIBRERÍA I2C LCD (Compacta)
// ==========================================
// Definimos los pines I2C (RB0 y RB1 en PIC18F4550)
#define I2C_SDA PORTBbits.RB0
#define I2C_SCL PORTBbits.RB1
#define I2C_SDA_DIR TRISBbits.TRISB0
#define I2C_SCL_DIR TRISBbits.TRISB1

void I2C_Init() {
    I2C_SDA_DIR = 1; I2C_SCL_DIR = 1; // Entradas (Flotantes) = 1 lógico en I2C
}

void I2C_Hold() { __delay_us(10); }

void I2C_Start() {
    I2C_SDA_DIR = 1; I2C_SCL_DIR = 1; I2C_Hold();
    I2C_SDA_DIR = 0; I2C_Hold(); // SDA Baja
    I2C_SCL_DIR = 0; I2C_Hold(); // SCL Baja
}

void I2C_Stop() {
    I2C_SDA_DIR = 0; I2C_Hold();
    I2C_SCL_DIR = 1; I2C_Hold();
    I2C_SDA_DIR = 1; I2C_Hold();
}

void I2C_Write(unsigned char data) {
    for(int i=0; i<8; i++) {
        if((data & 0x80) != 0) I2C_SDA_DIR = 1; else I2C_SDA_DIR = 0;
        data <<= 1;
        I2C_Hold();
        I2C_SCL_DIR = 1; I2C_Hold();
        I2C_SCL_DIR = 0; I2C_Hold();
    }
    // ACK (Ignoramos lectura del ACK para simplificar)
    I2C_SDA_DIR = 1; I2C_Hold();
    I2C_SCL_DIR = 1; I2C_Hold();
    I2C_SCL_DIR = 0; I2C_Hold();
}

// Control específico para el PCF8574 + LCD
// P7..P4 = D7..D4, P3=BL, P2=EN, P1=RW, P0=RS
void LCD_I2C_Write_Nibble(unsigned char val, unsigned char control) {
    unsigned char data = (val & 0xF0) | control | 0x08; // 0x08 enciende Backlight
    I2C_Start();
    I2C_Write(0x40); // Dirección del PCF8574 (A0-A2 a Tierra) -> 0x40 Write
    I2C_Write(data | 0x04); // EN = 1
    I2C_Write(data & 0xFB); // EN = 0
    I2C_Stop();
    __delay_us(50);
}

void LCD_I2C_Cmd(unsigned char cmd) {
    LCD_I2C_Write_Nibble(cmd & 0xF0, 0);     // Parte Alta
    LCD_I2C_Write_Nibble((cmd << 4) & 0xF0, 0); // Parte Baja
    if(cmd < 4) __delay_ms(2); // Comandos lentos (Clear/Home)
}

void LCD_I2C_Char(char data) {
    LCD_I2C_Write_Nibble(data & 0xF0, 1);     // RS = 1
    LCD_I2C_Write_Nibble((data << 4) & 0xF0, 1);
}

void LCD_I2C_String(char *str) {
    while(*str) LCD_I2C_Char(*str++);
}

void LCD_I2C_Init() {
    I2C_Init();
    __delay_ms(50);
    LCD_I2C_Write_Nibble(0x30, 0); __delay_ms(5);
    LCD_I2C_Write_Nibble(0x30, 0); __delay_us(150);
    LCD_I2C_Write_Nibble(0x30, 0);
    LCD_I2C_Write_Nibble(0x20, 0); // Modo 4 bits
    
    LCD_I2C_Cmd(0x28); // 2 líneas, 5x8
    LCD_I2C_Cmd(0x0C); // Display ON, Cursor OFF
    LCD_I2C_Cmd(0x06); // Incremento derecha
    LCD_I2C_Cmd(0x01); // Limpiar
}

void LCD_Set_Cursor(int row, int col) {
    if(row == 1) LCD_I2C_Cmd(0x80 + col - 1);
    if(row == 2) LCD_I2C_Cmd(0xC0 + col - 1);
}

// ==========================================
//   PROGRAMA PRINCIPAL
// ==========================================
int marcha_actual = 0; 
char buffer_lcd[17];

void main(void) {
    // 1. PUERTOS
    TRISDbits.TRISD1 = 0;   // Servo
    TRISCbits.TRISC2 = 0;   // PWM Velocidad
    TRISDbits.TRISD2 = 0;   // Dir A
    TRISDbits.TRISD3 = 0;   // Dir B
    
    TRISAbits.TRISA0 = 1;   // Pot Dirección
    TRISAbits.TRISA1 = 1;   // Pot Acelerador
    
    // Botones en RE0 y RE1
    TRISEbits.TRISE0 = 1;   
    TRISEbits.TRISE1 = 1;
    
    // ADC
    ADCON0 = 0b00000001; ADCON1 = 0b00001101; ADCON2 = 0b10101101;
    
    // PWM Motor
    PR2 = 255; CCPR1L = 0; T2CON = 0b00000111; CCP1CON = 0b00001100;   

    // INICIAR LCD I2C
    LCD_I2C_Init();
    
    LCD_Set_Cursor(1,1); LCD_I2C_String("SISTEMA RC I2C");
    LCD_Set_Cursor(2,1); LCD_I2C_String("CARGANDO...");
    __delay_ms(1000);
    LCD_I2C_Cmd(0x01); // Limpiar pantalla

    int valor_direccion = 0;
    int valor_acelerador = 0;
    int tiempo_servo = 0;
    unsigned int velocidad_calculada = 0;
    int porcentaje_potencia = 0;

    while(1) {
        // A. SENSORES
        ADCON0bits.CHS = 0; __delay_us(10); ADCON0bits.GO = 1; while(ADCON0bits.GO);
        valor_direccion = (ADRESH << 8) + ADRESL;

        ADCON0bits.CHS = 1; __delay_us(10); ADCON0bits.GO = 1; while(ADCON0bits.GO);
        valor_acelerador = (ADRESH << 8) + ADRESL;

        // B. BOTONES (RE0 / RE1)
        if (PORTEbits.RE0 == 1) { 
            if (marcha_actual < 6) { marcha_actual++; __delay_ms(250); LCD_I2C_Cmd(0x01); }
        }
        if (PORTEbits.RE1 == 1) {
            if (marcha_actual > -1) { marcha_actual--; __delay_ms(250); LCD_I2C_Cmd(0x01); }
        }

        // C. LCD
        LCD_Set_Cursor(1,1);
        if (marcha_actual == 0) {
            LCD_I2C_String("MARCHA: N       ");
            porcentaje_potencia = 0;
        } else if (marcha_actual == -1) {
            LCD_I2C_String("MARCHA: R       ");
            porcentaje_potencia = (valor_acelerador * 100) / 1023;
        } else {
            sprintf(buffer_lcd, "MARCHA: %d       ", marcha_actual);
            LCD_I2C_String(buffer_lcd);
            porcentaje_potencia = (valor_acelerador * 100) / 1023;
        }

        LCD_Set_Cursor(2,1);
        sprintf(buffer_lcd, "POTENCIA: %d%%   ", porcentaje_potencia);
        LCD_I2C_String(buffer_lcd);

        // D. MOTOR
        velocidad_calculada = valor_acelerador / 4;
        if (marcha_actual == 0) {
            LATDbits.LATD2 = 0; LATDbits.LATD3 = 0; CCPR1L = 0;
        }
        else if (marcha_actual == -1) {
            LATDbits.LATD2 = 0; LATDbits.LATD3 = 1; 
            CCPR1L = (velocidad_calculada * 40) / 100;
        }
        else {
            LATDbits.LATD2 = 1; LATDbits.LATD3 = 0;
            float factor = 0;
            switch(marcha_actual) {
                case 1: factor = 0.16; break;
                case 2: factor = 0.33; break;
                case 3: factor = 0.50; break;
                case 4: factor = 0.66; break;
                case 5: factor = 0.83; break;
                case 6: factor = 1.00; break;
            }
            CCPR1L = velocidad_calculada * factor;
        }

        // E. DIRECCIÓN (Servo)
        unsigned long calculo = (unsigned long)valor_direccion * 25; 
        tiempo_servo = 250 + (calculo / 10);
        LATDbits.LATD1 = 1; 
        int ciclos = tiempo_servo / 10;
        for(int i=0; i < ciclos; i++) __delay_us(10); 
        LATDbits.LATD1 = 0; 
        
        __delay_ms(15); 
    }
}