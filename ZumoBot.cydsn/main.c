#include <project.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "Motor.h"
#include "Ultra.h"
#include "Nunchuk.h"
#include "Reflectance.h"
#include "I2C_made.h"
#include "Gyro.h"
#include "Accel_magnet.h"
#include "IR.h"
#include "Ambient.h"
#include "Beep.h"

int rread(void);

float readBatteryVoltage();
float getPIDValue(float, float, float*, float*);
void scaleInput(float*);

int main()
{
    CyGlobalIntEnable; 
    UART_1_Start();
    ADC_Battery_Start();   
    
    sensor_isr_StartEx(sensor_isr_handler);
    reflectance_start();
    motor_start();
    IR_led_Write(1);
    
    struct sensors_ sensors;
    float volts = 0.0;
    int loopTime = 600000000;
    int ledTime = 0;
    int ledState = 0;
    int buttonState = 1;
    int buttonValue = 0;
    float speed = 255;
    
    int white[4] = { 0, 0, 0, 0 };
    int black[4] = { 0, 0, 0, 0 };
    float status[4] = { 0, 0, 0, 0 };
    
    int delay = 10;
    
    int state = 1;
    int start = 5;
    int stop = 0;
    int stopCount = 0;
    int stopTime = 0;
    
    int driveToStart = 0;

    printf("\nBoot\n");
    
    // Aseta moottoreiden alkuarvo nollaan
    motor_forward(0, 10);
    
    // Alusta satunnainen
    srand(time(0));

    for(;;)
    {
        ledTime++;
        loopTime++;
        
        /*
            Mittaa akun volttimäärän joka 30 sekunti
        */
        if (loopTime >= 300 * (100 / delay)) {
            volts = readBatteryVoltage();
            loopTime = 0;
        }
        
        /*
            Tarkistaa että pattereissa on tarpeeksi tehoa jäljellä, jos ei ole, keskeyttää ohjelman tähän
        */
        if (volts <= 4 && volts != 0) {
            if (ledTime >= 1 * (100 / delay)) {
                ledState = !ledState;
                BatteryLed_Write(ledState);
                ledTime = 0;
            }
            
            CyDelay(delay);
            
            continue;
        } else if (ledState == 1 && state > 2) {
            ledState = !ledState;
            BatteryLed_Write(ledState);
        }
        
        /*
            Muuntaa napin painalluksesta yksittäisen pulssin
        */
        int button = SW1_Read();
        if (!button && buttonState) {
            buttonState = 0;
            buttonValue = 0; 
        } else {
            buttonValue = 1;
        }
        
        if (button && !buttonState) buttonState = 1;
        
        switch (state) {
            /*
                Kalibroi valkoisen värin
            */
            case 1:
                if (ledTime >= 4 * (100 / delay)) {
                    ledState = !ledState;
                    BatteryLed_Write(ledState);
                    ledTime = 0;
                }
                
                if (buttonValue == 0) {
                    reflectance_read(&sensors);
                    white[0] = sensors.l3;
                    white[1] = sensors.l1;
                    white[2] = sensors.r1;
                    white[3] = sensors.r3;
                    
                    Beep(10, 100);
                    
                    state++;
                }
                
                break;
               
            /*
                Kalibroi mustan värin
            */
            case 2:
                if (ledTime >= 8 * (100 / delay)) {
                    ledState = !ledState;
                    BatteryLed_Write(ledState);
                    ledTime = 0;
                }
                
                if (buttonValue == 0) {
                    reflectance_read(&sensors);
                    black[0] = sensors.l3;
                    black[1] = sensors.l1;
                    black[2] = sensors.r1;
                    black[3] = sensors.r3;
                    
                    Beep(10, 200);
                    
                    state++;
                }
                
                break;
            
            /*
                Ajaa hitaasti maaliviivalle
            */
            case 3:
                reflectance_read(&sensors);
                
                if (buttonValue == 0) driveToStart = 1;
                
                if (!driveToStart) break;
                
                if (sensors.l3 > black[0] - 100 && sensors.r3 > black[3] - 100) {
                    motor_forward(0, delay);
                    state++;
                } else {
                    motor_forward(80, delay);
                }
                
                break;
                
            /*
                Piippaa valmiuden merkiksi, ja jää odottamaan kaukosäätimen painallusta  
            */
            case 4:
                Beep(10, 100);
                
                get_IR();
                
                state++;
                break;
            
            /*
                Seuraa viivaa ja pysähtyy maaliin  
            */
            case 5:
                // Luo antureista kalibroidut arvot välille 0.0 - 1.0
                reflectance_read(&sensors);
                status[0] = (float)(sensors.l3 - white[0]) / (float)(black[0] - white[0]);
                status[1] = (float)(sensors.l1 - white[1]) / (float)(black[1] - white[1]);
                status[2] = (float)(sensors.r1 - white[2]) / (float)(black[2] - white[2]);
                status[3] = (float)(sensors.r3 - white[3]) / (float)(black[3] - white[3]);
                
                // Varmistaa että arvot pysyvät 0 ja 1 välillä, ja muuntelee niitä tarvittaessa
                scaleInput(&status[0]);
                scaleInput(&status[1]);
                scaleInput(&status[2]);
                scaleInput(&status[3]);
                
                if (start > 0) {
                    start--;
                    motor_forward(speed, delay * 10);
                    CyDelay(delay * 10);
                    break;
                }
                
                float over = 0.8;
                int amount = 0;
                if (status[0] > over) amount++;
                if (status[1] > over) amount++;
                if (status[2] > over) amount++;
                if (status[3] > over) amount++;
                
                printf("%f, %f, %f, %f \n", status[0], status[1], status[2], status[3]);
                
                if (amount >= 2) {
                    printf("Rot \n");
                    
                    int distance = (rand() % (4 + 1 - 4) + 4) * 100;
                    motor_backward(speed, distance);
                    CyDelay(distance);
                    
                    int rotation = (rand() % (6 + 1 - 3) + 3) * 100;
                    if (rand() % 10 > 5) {
                        motor_turn(0, speed, rotation);
                        CyDelay(rotation);
                    } else {
                        motor_turn(0, speed, rotation);
                        CyDelay(rotation); 
                    }
                }
                
                motor_forward(speed, delay);
                
                // Pysähdy jos nappia painetaan
                if (buttonValue == 0) state++;
                
                break;
                
            default:
                motor_forward(0, delay);
                break;
        }
        
        CyDelay(delay);
    }
}

float readBatteryVoltage()
{
    int16 adcresult = 0;
    ADC_Battery_StartConvert();
    if(ADC_Battery_IsEndConversion(ADC_Battery_WAIT_FOR_RESULT)) {
        adcresult = ADC_Battery_GetResult16();
        return ((float)adcresult / 4095) * 5 * 1.5;
    }
    return 0;
}

float getPIDValue(float current, float target, float *integral, float *lastError)
{
    float kp = 0.7;
    float ki = 0.4;
    float kd = 0.0;

    float error = target - current;
    *integral = *integral + error;
    float derivate = error - *lastError;
    
    float pwm = (kp * error) + (ki * *integral) + (kd * derivate);

    if (pwm > 1) pwm = 1;
    else if (pwm < 0) pwm = 0;
    
    
    *lastError = error;
    
    return pwm;
}

void scaleInput(float *input)
{
    *input = *input * 1.25;
    if (*input < 0) *input = 0;
    if (*input > 1) *input = 1;
}

/* Don't remove the functions below */
int _write(int file, char *ptr, int len)
{
    (void)file; /* Parameter is not used, suppress unused argument warning */
	int n;
	for(n = 0; n < len; n++) {
        if(*ptr == '\n') UART_1_PutChar('\r');
		UART_1_PutChar(*ptr++);
	}
	return len;
}

int _read (int file, char *ptr, int count)
{
    int chs = 0;
    char ch;
 
    (void)file; /* Parameter is not used, suppress unused argument warning */
    while(count > 0) {
        ch = UART_1_GetChar();
        if(ch != 0) {
            UART_1_PutChar(ch);
            chs++;
            if(ch == '\r') {
                ch = '\n';
                UART_1_PutChar(ch);
            }
            *ptr++ = ch;
            count--;
            if(ch == '\n') break;
        }
    }
    return chs;
}
/* [] END OF FILE */
