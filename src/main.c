#define F_CPU 16000000UL // ATmega128의 클럭 주파수, 16MHz
#include <avr/io.h> // AVR 입출력 포트 정의 헤더 파일
#include <avr/interrupt.h> // 인터럽트 헤더 파일
#include <util/delay.h> // 딜레이 함수 헤더 파일

#define TIMER_MODE 0
#define AIR_QUALITY_MODE 1
#define WARNING_LEVEL 200 // 공기질 경고 농도

unsigned char fnd_digit[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 
    0x6D, 0x7D, 0x07, 0x7F, 0x6F}; // 0 ~ 9 숫자 패턴
unsigned char fnd_select[4] = {0x08, 0x04, 0x02, 0x01}; // FND 선택 패턴

int mode = TIMER_MODE; 
unsigned int timer_seconds = 3600;
volatile int air_quality = 0;

volatile unsigned int count = 0; // 타이머 카운트
volatile unsigned int last_switch1_time = 0; // 마지막 스위치 눌린 시간
volatile unsigned int switch2_mode = 0; // 스위치 2 현재 모드

volatile unsigned char fnd_index = 0; // FND 갱신용 인덱스
volatile unsigned char fnd_value[4]; // FND 출력 데이터

/* ADC 초기화 */
void adc_init() {
    ADMUX = (1 << REFS0); // AVcc를 기준 전압으로 설정 (5V)
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1); // ADC 활성화, 프리스케일러 64
}

/* ADC 변환 시작 및 값 읽기 */
int adc_read(unsigned char channel) {
    channel &= 0x07; // 채널 제한 (0~7)
    ADMUX = (ADMUX & 0xF8) | channel; // 입력 채널 설정
    ADCSRA |= (1 << ADSC); // 변환 시작
    while (ADCSRA & (1 << ADSC)); // 변환 완료 대기
    return ADCW; // 변환 결과 반환
}

/* Timer0 비교 일치 인터럽트: FND 갱신 */
ISR(TIMER0_COMPA_vect) {
    PORTC = fnd_value[fnd_index]; // FND 데이터 출력
    PORTG = fnd_select[fnd_index]; // FND 선택 출력
    fnd_index = (fnd_index + 1) % 4; // 다음 세그먼트로 이동
}

/* switch 1 눌렀을때 */
// INT4: state 토글
ISR(INT4_vect) {
    mode = AIR_QUALITY_MODE;
    last_switch1_time = count; // 스위치 눌린 시간 기록
}

/* switch 2 눌렀을때 */
// INT5: state OFF, count 초기화
ISR(INT5_vect) {
    switch2_mode = !switch2_mode;
    mode = (switch2_mode == 1) ? AIR_QUALITY_MODE : TIMER_MODE;
}

// Timer1 비교 일치 인터럽트: count 증가
ISR(TIMER1_COMPA_vect) {
    count++; // 1초마다 count 증가

    if (timer_seconds > 0) {
        timer_seconds--;
    } else {
        PORTB |= (1 << PB4);
        _delay_ms(100);
        PORTB &= ~(1 << PB4);
        timer_seconds = 3600;
    }
    
    if (air_quality > WARNING_LEVEL) {
        PORTB |= (1 << PB4);
        _delay_ms(100);
        PORTB &= ~(1 << PB4);
        timer_seconds = 3600;
    }
}

void fnd_print(int value) {
    if (mode == AIR_QUALITY_MODE) {
        fnd_value[0] = fnd_digit[(value / 1000) % 10];
        fnd_value[1] = fnd_digit[(value / 100) % 10];
        fnd_value[2] = fnd_digit[(value / 10) % 10];
        fnd_value[3] = fnd_digit[value % 10];
    } else {
        int minutes = value / 60; // 분
        int seconds = value % 60; // 초
        fnd_value[3] = fnd_digit[(minutes / 10) % 10];
        fnd_value[2] = fnd_digit[minutes % 10];
        fnd_value[1] = fnd_digit[(seconds / 10) % 10];
        fnd_value[0] = fnd_digit[seconds % 10];
    }
}

int main(void) {
    DDRA = 0xFF; // 포트 A를 출력으로 설정 LED
    DDRE = 0x00; // 포트 E를 입력으로 설정 스위치
    DDRB = 0xFF; // 부저
    DDRC = 0xFF; // 포트 C를 FND 데이터 출력으로 설정
    DDRG = 0x0F; // 포트 G를 FND 선택으로 설정 

    // Timer1 설정: CTC 모드, 프리스케일러 1024, 1초 주기
    TCCR1B |= (1 << WGM12); // CTC 모드 설정
    TCCR1B |= (1 << CS12) | (1 << CS10); // 프리스케일러 1024 설정
    OCR1A = 15625; // 비교 일치 값 설정 (16MHz / 1024 / 1Hz)
    TIMSK |= (1 << OCIE1A); // Timer1 비교 일치 인터럽트 활성화

    // Timer0 설정: CTC 모드, 프리스케일러 64, FND 갱신용
    TCCR0 |= (1 << WGM01); // CTC 모드 설정
    TCCR0 |= (1 << CS01) | (1 << CS00); // 프리스케일러 64 설정
    OCR0 = 249; // 1ms 주기 (16MHz / 64 / 250)
    TIMSK |= (1 << OCIE0); // Timer0 비교 일치 인터럽트 활성화

    /* switch1, switch2 눌렀을 때 작동하도록 하는 코드 */
    EICRB = 0xAA; // INT4와 INT5를 하강 에지에서 인터럽트 발생
    EIMSK = 0x30; // INT4, INT5 인터럽트 활성화
    sei(); // 전역 인터럽트 허용

    adc_init(); // ADC 초기화

    while (1) {
        if (switch2_mode == 0) {
            if (mode == AIR_QUALITY_MODE && count - last_switch1_time > 1) {
                mode = TIMER_MODE;
            }
        }

        air_quality = adc_read(0); // ADC 채널 0에서 공기질 데이터 읽기

        if (mode == TIMER_MODE)
            fnd_print(timer_seconds);
        else if (mode == AIR_QUALITY_MODE)
            fnd_print(air_quality);
    }

    return 0;
}
