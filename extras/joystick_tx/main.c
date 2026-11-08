#include <stdint.h>

#define RCC_APB2ENR   (*(volatile uint32_t*)0x40021018)
#define GPIOA_CRL     (*(volatile uint32_t*)0x40010800)
#define GPIOA_CRH     (*(volatile uint32_t*)0x40010804)
#define GPIOA_IDR     (*(volatile uint32_t*)0x40010808)
#define GPIOA_ODR     (*(volatile uint32_t*)0x4001080C)
#define ADC1_SR       (*(volatile uint32_t*)0x40012400)
#define ADC1_CR2      (*(volatile uint32_t*)0x40012408)
#define ADC1_SQR3     (*(volatile uint32_t*)0x40012434)
#define ADC1_DR       (*(volatile uint32_t*)0x4001244C)
#define USART1_SR     (*(volatile uint32_t*)0x40013800)
#define USART1_DR     (*(volatile uint32_t*)0x40013804)
#define USART1_BRR    (*(volatile uint32_t*)0x40013808)
#define USART1_CR1    (*(volatile uint32_t*)0x4001380C)

static void delay(volatile uint32_t n) { while(n--); }

static void uart_ch(char c) {
    while (!(USART1_SR & (1<<7)));
    USART1_DR = c;
}

static void uart_num(uint16_t n) {
    char buf[5];
    int i = 0;
    if (n == 0) { uart_ch('0'); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) uart_ch(buf[--i]);
}

static uint16_t adc_read(uint8_t ch) {
    ADC1_SQR3 = ch;
    ADC1_CR2 |= (1<<0);
    while (!(ADC1_SR & (1<<1)));
    return (uint16_t)(ADC1_DR & 0xFFF);
}

int main(void) {
    RCC_APB2ENR |= (1<<2) | (1<<9) | (1<<14);

    GPIOA_CRL = 0x00000800;
    GPIOA_ODR |= (1<<2);
    GPIOA_CRH = (GPIOA_CRH & 0xFFFFFF0F) | 0xB0;

    USART1_BRR = 833;
    USART1_CR1 = (1<<13) | (1<<3);

    ADC1_CR2 = (1<<0);
    delay(10000);

    while (1) {
        uint16_t x = adc_read(0);
        uint16_t y = adc_read(1);
        uint8_t b = !(GPIOA_IDR & (1<<2));

        uart_num(x);
        uart_ch(',');
        uart_num(y);
        uart_ch(',');
        uart_ch(b ? '1' : '0');
        uart_ch('\r');
        uart_ch('\n');

        delay(400000);
    }
}

extern uint32_t _estack;
__attribute__((section(".isr_vector")))
uint32_t *vectors[] = {
    (uint32_t*)&_estack,
    (uint32_t*)main
};
