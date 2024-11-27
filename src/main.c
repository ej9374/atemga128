#define F_CPU 16000000UL // ATmega128의 클럭 주파수, 16MHz
#include <avr/io.h> // AVR 입출력 포트 정의 헤더 파일
#include <avr/interrupt.h> // 인터럽트 헤더 파일
#include <util/delay.h> // 딜레이 함수 헤더 파일

#define TIMER_MODE 0
#define AIR_QUALITY_MODE 1
#define WARNING_LEVEL 200 //공기질 경고 농도


/* TODO air_quality 설정하기 <- 공기질센서 이용하기 */

unsigned char fnd_digit[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 
    0x6D, 0x7D, 0x07, 0x7F, 0x6F}; // 0 ~ 9 숫자 패턴
unsigned char fnd_select[4] = {0x08, 0x04, 0x02, 0x01}; // FND 선택 패턴

int mode = TIMER_MODE; 
unsigned int timer_seconds = 3600;
int air_quality = 0;

volatile unsigned int count = 0;


/* switch 1 눌렀을때*/
// INT4: state 토글
ISR(INT4_vect) {
    mode = AIR_QUALITY_MODE;
    _delay_ms(5000);
    mode = TIMER_MODE;
}

/* switch 2 눌렀을때*/
// INT5: state OFF, count 초기화
ISR(INT5_vect) {
    mode = (mode == TIMER_MODE) ? AIR_QUALITY_MODE : TIMER_MODE;
}

// Timer1 비교 일치 인터럽트: count 증가
ISR(TIMER1_COMPA_vect) {
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


void fnd_print(int value, int air_quality) {
    unsigned char fnd_value[4];

    if (mode == AIR_QUALITY_MODE){
        fnd_value[0] = fnd_digit[(value/1000) % 10];
        fnd_value[1] = fnd_digit[(value/100) % 10];
        fnd_value[2] = fnd_digit[(value/10) % 10];
        fnd_value[3] = fnd_digit[value % 10];
    } else {
        int minutes = value / 60; //분
        int seconds = value % 60; //초
        fnd_value[3] = fnd_digit[(minutes/10) % 10];
        fnd_value[2] = fnd_digit[minutes % 10];
        fnd_value[1] = fnd_digit[(seconds/10) % 10];
        fnd_value[0] = fnd_digit[seconds % 10];
    }


    for (int i = 0; i < 4; i++) {
        PORTC = fnd_value[i] | ((mode == TIMER_MODE) && i == 2 ? 0x80 : 0x00);
        PORTG = 0x01 << i;
        _delay_ms(2);
    }

}

int main(void) {
    DDRA = 0xFF; // 포트 A를 출력으로 설정 LED
    DDRE = 0x00; // 포트 E를 입력으로 설정 스위치
    DDRB = 0xFF; // 부저
    DDRC = 0xFF; // 포트 C를 FND 데이터 출력으로 설정
    DDRG = 0x0F; // 포트 G를 FND 선택으로 설정 


    // Timer1 설정: CTC 모드, 프리스케일러 64, 1초 주기
    
    TCCR1B |= (1 << WGM12); // CTC 모드 설정
    TCCR1B |= (1 << CS12) | (1 << CS10); // 프리스케일러 1024 설정
    OCR1A = 15625; // 비교 일치 값 설정 (16MHz / 1024/ 1Hz)
    TIMSK |= (1 << OCIE1A); // Timer1 비교 일치 인터럽트 활성화
    
    /*switch1 switch2 눌렀을때 작동하도록 하는 코드*/
    EICRB = 0xAA; // INT4와 INT5를 하강 에지에서 인터럽트 발생
    EIMSK = 0x30; // INT4, INT5 인터럽트 활성화
    sei(); // 전역 인터럽트 허용

    while (1) {
        if (mode == TIMER_MODE){
            fnd_print(timer_seconds, 0);
        } else if (mode == AIR_QUALITY_MODE){
            fnd_print(air_quality, 1);
        }
    }

    return 0;
}
