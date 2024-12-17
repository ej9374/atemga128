#define F_CPU 16000000UL // ATmega128의 클럭 주파수, 16MHz
#include <avr/io.h> // AVR 입출력 포트 정의 헤더 파일
#include <avr/interrupt.h> // 인터럽트 헤더 파일
#include <util/delay.h> // 딜레이 함수 헤더 파일

#define TIMER_MODE 0
#define AIR_QUALITY_MODE 1
#define WARNING_LEVEL 240 // 공기질 경고 농도 (0~1000)

unsigned char fnd_digit[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 
    0x6D, 0x7D, 0x07, 0x7F, 0x6F}; // 0 ~ 9 숫자 패턴
unsigned char fnd_select[4] = {0x08, 0x04, 0x02, 0x01}; // FND 선택 패턴

volatile int mode = TIMER_MODE; 
unsigned int timer_seconds = 3600;
int air_quality = 0;

volatile unsigned int count = 0; // 프로그램 전체 카운트
volatile unsigned int buzzer_timer = 0; // 부저 타이머

/* switch 1 눌렀을때 */
// INT4: AIR_QUALITY_MODE와 TIMER_MODE 전환
ISR(INT4_vect) {
    mode = (mode == TIMER_MODE) ? AIR_QUALITY_MODE : TIMER_MODE;
}

/* Timer1 비교 일치 인터럽트: count 증가 */
ISR(TIMER1_COMPA_vect) {
    count++; // 1초마다 count 증가

    if (timer_seconds > 0) {
        timer_seconds--;
    } else {
        buzzer_timer = 100; // 부저 타이머 설정 (100ms)
        timer_seconds = 3600; // 초기화
    }

}

void fnd_print(int value, int air_quality) {
    unsigned char fnd_value[4];

    if (mode == AIR_QUALITY_MODE) {
        fnd_value[3] = fnd_digit[(value / 1000) % 10];
        fnd_value[2] = fnd_digit[(value / 100) % 10];
        fnd_value[1] = fnd_digit[(value / 10) % 10];
        fnd_value[0] = fnd_digit[value % 10];
    } else {
        int minutes = value / 60; // 분
        int seconds = value % 60; // 초
        fnd_value[3] = fnd_digit[(minutes / 10) % 10];
        fnd_value[2] = fnd_digit[minutes % 10];
        fnd_value[1] = fnd_digit[(seconds / 10) % 10];
        fnd_value[0] = fnd_digit[seconds % 10];
    }

    for (int i = 0; i < 4; i++) {
        PORTC = fnd_value[i] | ((mode == TIMER_MODE) && i == 2 ? 0x80 : 0x00);
        PORTG = 0x01 << i;
        _delay_ms(2);
    }
}

void adc_init(void) {
    // ADC 초기화: AVCC를 기준 전압으로 설정, 프리스케일러 128
    ADMUX = (1 << REFS0); // AVCC를 기준 전압으로 설정
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0); // ADC 활성화 및 프리스케일러 128 설정
}

uint16_t adc_read(uint8_t ch) {
    // ADC 채널 선택
    ch &= 0b00000111; // ADC0 ~ ADC7
    ADMUX = (ADMUX & 0xF8) | ch;

    // ADC 변환 시작
    ADCSRA |= (1 << ADSC);

    // 변환 완료 대기
    while (ADCSRA & (1 << ADSC));

    return ADC;
}

float convert_to_ppm(uint16_t adc_value) {
    float voltage = adc_value * (5.0 / 1024.0); // ADC 값을 전압으로 변환 (0~5V)
    float ppm = (voltage - 0.1) / 0.02; // 전압을 농도로 변환 (기준 전압: 0.1V, 스케일링 팩터: 0.02V/ppm)
    return ppm;
}

int main(void) {
    DDRF = 0x00; // 포트 A를 입력으로 설정 (ADC)
    DDRE = 0x00; // 포트 E를 입력으로 설정 스위치
    PORTE |= (1 << PE4) | (1 << PE5); // INT4, INT5 풀업 저항 활성화
    DDRB |= (1 << PB4); // 부저
    DDRC = 0xFF; // 포트 C를 FND 데이터 출력으로 설정
    DDRG = 0x0F; // 포트 G를 FND 선택으로 설정 

    // Timer1 설정: CTC 모드, 프리스케일러 1024, 1초 주기
    TCCR1B |= (1 << WGM12); // CTC 모드 설정
    TCCR1B |= (1 << CS12) | (1 << CS10); // 프리스케일러 1024 설정
    OCR1A = 15625; // 비교 일치 값 설정 (16MHz / 1024 / 1Hz)
    TIMSK |= (1 << OCIE1A); // Timer1 비교 일치 인터럽트 활성화

    /* switch1, switch2 눌렀을때 작동하도록 설정 */
    EICRB = 0xAA; // INT4와 INT5를 하강 에지에서 인터럽트 발생
    EIMSK = 0x30; // INT4, INT5 인터럽트 활성화
    sei(); // 전역 인터럽트 허용

    adc_init(); // ADC 초기화

    while (1) {
        air_quality = adc_read(0); // ADC0 채널에서 공기질 값 읽기
        float air_quality_ppm = convert_to_ppm(air_quality); // 공기질 값을 농도로 변환

        // FND 출력
        if (mode == TIMER_MODE) {
            fnd_print(timer_seconds, 0);
        } else if (mode == AIR_QUALITY_MODE) {
            fnd_print((int)air_quality_ppm, 1); // 농도를 정수로 변환하여 FND에 출력
        }

        if (timer_seconds == 0 || air_quality_ppm > WARNING_LEVEL) {
            buzzer_timer = 100; // 부저 타이머 설정 (100ms)
        }

        if (buzzer_timer > 0) {
            if (timer_seconds == 0) {
                // 타이머 모드: 500Hz
                PORTB |= (1 << PB4); // 부저 ON
                _delay_us(1000); // 500Hz 주파수
                PORTB &= ~(1 << PB4); // 부저 OFF
                _delay_us(1000); // 500Hz 주파수
            } else if (air_quality_ppm > WARNING_LEVEL) {
                // 공기질 모드: 2000Hz
                PORTB |= (1 << PB4); // 부저 ON
                _delay_us(250); // 2000Hz 주파수
                PORTB &= ~(1 << PB4); // 부저 OFF
                _delay_us(250); // 2000Hz 주파수
            }
            buzzer_timer--;
        }
    }

    return 0;
}
