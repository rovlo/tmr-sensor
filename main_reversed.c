#include <msp430.h> 

#define TimerA_constant 12000
#define TXD BIT1
#define Bit_time 104 //1mhz / 9600

unsigned int TXByte, ADCValue, i;
signed int fourthValue, previousValue, startValue, currentValue, minValue, maxValue;
unsigned char bitCount, j, signalFalling, signalRising,
        passThroughZero, tMin, tMax, transmitFlag;

void Clock_config(void)
{
    BCSCTL2 = SELM_0 | DIVM_0 | DIVS_0; //SELM_0 - Master Clock DCOCLK

    if (CALBC1_1MHZ != 0xFF)
    {
        /* Follow recommended flow. First, clear all DCOx and MODx bits. Then
         * apply new RSELx values. Finally, apply new DCOx and MODx bit values.
         */
        DCOCTL = 0x00;
        BCSCTL1 = CALBC1_1MHZ; /* Set DCO to 1MHz */
        DCOCTL = CALDCO_1MHZ;
    }

    BCSCTL1 |= XT2OFF; // XT2OFF - turn off XT2 oscillator, DIVA_3 - divide ACLK by 8 - 1.5kHz
    BCSCTL3 = XT2S_0 | LFXT1S_2 | XCAP_1; // ACLK = VLO
}

void ADC10_config(void)
{
    ADC10CTL0 &= ~ENC;

    ADC10AE0 |= BIT4;

    ADC10CTL0 = ADC10SR + SREF_0 + ADC10IE + ADC10ON;
    ADC10CTL1 = INCH_4 + CONSEQ_0 + ADC10SSEL_1;

    ADC10CTL0 |= ENC;
}

void Timer_config(void)
{
    TAR = 0;
    TACTL = TASSEL_1; //TACLK = ACLK = VLO
    TACCTL0 = CCIE; // capture compare interrupt enable
    TACCR0 = TimerA_constant; // 12000/12000Hz = 1s
    TACTL |= MC_1; //Up mode, TA start
}

void Transmit()
{
    TXByte |= 0x100;              // Add stop bit to TXByte (which is logical 1)
    TXByte = TXByte << 1;               // Add start bit (which is logical 0)
    bitCount = 10;                   // Load Bit counter, 8 bits + ST/SP
    transmitFlag = 1;
    __bic_SR_register(GIE);
    __bis_SR_register(GIE);

    TACCTL0 = OUT;                    
    
    TACCR0 = Bit_time;               // Set time till first bit
    TAR = 0;
    TACCTL0 = CCIS0 + OUTMOD0 + CCIE; // Set signal, initial value, interrupt enable
    TACTL = TASSEL_2 + MC_1;
    while (TACCTL0 & CCIE);             // Wait for previous TX completion
    TACTL &= ~MC_1;
    transmitFlag = 0;
    Timer_config();
}

int main(void)
{
    WDTCTL = WDTPW | WDTHOLD;   // stop watchdog timer

    P1DIR = BIT0 + BIT1 + BIT6;
    P1OUT &= ~(BIT0 + BIT6);
    P1SEL |= BIT1;
    Clock_config();
    ADC10_config();

    startValue = 0;

    __bis_SR_register(GIE);

    __delay_cycles(100000);
    for (i = 0; i < 5; i++)
    {
        ADC10CTL0 |= ADC10SC; // start ADC - single channel no repeat
        __bis_SR_register(LPM3_bits);
        startValue += ADC10MEM; // add conversion result
    }
    startValue = startValue / 5; // average out conversion results

    Timer_config();

    __bis_SR_register(LPM3_bits);

    return 0;
}

#pragma vector = ADC10_VECTOR
__interrupt void ADC10(void)
{
    __bic_SR_register(LPM3_bits);
    ADCValue = ADC10MEM;
    TXByte = ADCValue & 0x00FF;
    Transmit();
    __delay_cycles(1000);
    TXByte = ADCValue >> 8;
    TXByte = TXByte & 0x00FF;
    Transmit();
    __delay_cycles(1000);
    __bic_SR_register_on_exit(LPM3_bits);
}

#pragma vector = TIMERA0_VECTOR
__interrupt void Timer_A(void)
{

    if (!transmitFlag)
    {
        TACTL &= ~MC_1; // stop timer

        P1OUT |= BIT0;

        ADC10CTL0 |= ADC10SC;
        __bic_SR_register(GIE);
        __bis_SR_register(LPM3_bits + GIE);
        currentValue = ADC10MEM;

        if (currentValue - startValue > 5 || currentValue - startValue < -5)
        {
            __bic_SR_register(LPM3_bits); //turn on CPU
            //activity detected
            //reconfigure ADC10 for slow sampling

            ADC10CTL0 &= ~ENC;
            BCSCTL1 |= DIVA_1; //slow down ACLK by 2 -> 6000 Hz
            ADC10CTL0 |= ADC10SHT_3; // slow down sampling rate to 6000/(13 ADC10CLK + 64 SHT) = 70 Hz

            previousValue = currentValue - startValue;
            maxValue = previousValue;
            minValue = previousValue;

            ADC10CTL0 |= ENC;
            tMax = 0;
            tMin = 0;
            signalFalling = 0;
            signalRising = 0;
            passThroughZero = 0;

            // measure 200 samples, do some comparison
            for (i = 1; i < 50; i++)
            {
                currentValue = 0;

                for (j = 0; j < 4; j++){
                    ADC10CTL0 |= ADC10SC;
                    __bic_SR_register(GIE);
                    __bis_SR_register(LPM3_bits + GIE);

                    currentValue += ADC10MEM;
                }
                currentValue /= 4;
                currentValue -= startValue; // last measured value

                if (currentValue < previousValue)
                {
                    signalFalling++;
                }
                else if (currentValue > previousValue)
                {
                    signalRising++;
                }
                if ((currentValue >= 0 && previousValue < 0
                        || currentValue <= 0 && previousValue > 0))
                {
                    passThroughZero++;
                }
                if (currentValue >= maxValue)
                {
                    maxValue = currentValue;
                    tMax = i;
                }
                if (currentValue <= minValue)
                {
                    minValue = currentValue;
                    tMin = i;
                }

                previousValue = currentValue;

            }

            if (passThroughZero >= 2 && passThroughZero <= 7 && signalRising >= 10
                    && signalFalling <= 30 && tMin <= 30 && tMax >= tMin
                    && tMax <= 45
                    )
            {
                P1OUT |= BIT6;
            }

            ADC10CTL0 &= ~ENC;
            ADC10CTL0 &= ~ADC10SHT_3;
            BCSCTL1 &= ~DIVA_1;
            ADC10CTL0 |= ENC;

            __bis_SR_register_on_exit(LPM3_bits);

        }

        P1OUT &= ~BIT0;
        TACTL |= MC_1;

    }
    else
    {
        TACCR0 = Bit_time;
        if (bitCount == 0)
        {
            TACTL = TASSEL_2;
            TACCTL0 &= ~CCIE;
        }
        else
        {
            TACCTL0 |= OUTMOD2;          // Set TX bit to 0
            if (TXByte & 0x01)
                TACCTL0 &= ~OUTMOD2;     // If it should be 1, set it to 1
            TXByte = TXByte >> 1;
            bitCount--;
        }
    }
}
