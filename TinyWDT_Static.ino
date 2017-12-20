#include <avr/wdt.h>

const int host_reset_pin   = 0;                        // PB0 = DIG0
const int configure_tx_pin = 1;                        // PB1 = DIG1 (not connected to anything, defined only for the benefit of SoftSerial 
const int led_pin          = 2;                        // PB2 = DIG2
const int pet_input_pin    = 3;                        // PB3 = DIG3
const int debug_pin        = 4;                        // PB4 = DIG4
uint8_t debug_state        = 0;

#define LED_SHORT_BLINK_DURATION_MS      (50)
#define PET_WATCHDOG_RELEASE_DURATION_MS (5)
#define SETUP_TIMEOUT_DURATION_MS        (10000UL)

void wdt_init(void) __attribute__((naked)) __attribute__((section(".init3")));
void wdt_init(void){
    MCUSR = 0;
    wdt_disable();
    return;
}
#define soft_reset()        \
do                          \
{                           \
    wdt_enable(WDTO_15MS);  \
    for(;;)                 \
    {                       \
    }                       \
} while(0)

uint32_t ms_without_being_pet = 0;
uint32_t min_wait_period_after_petting_ms = 100UL;
uint32_t maximum_wait_period_after_petting_ms = 65000UL;
volatile uint8_t led_on_duration_ms = 0;
volatile uint8_t check_for_pet_timer_ms = 0;
volatile uint8_t once_per_millisecond_timer_ms = 0;
volatile uint16_t setup_timeout_timer_ms = SETUP_TIMEOUT_DURATION_MS;

volatile uint8_t internal_watchdog_timeout_timer_ms = 0;
volatile uint8_t debug_transition_timer_ms = 0;
uint8_t debug_transition_reload_ms = 100; // transition every 100ms

boolean first_pet = true; // no minimum window constraint on the first pet

// this ISR is set up to fire once a millisecond
// and only impacts 1-byte volatile variables
// in order to avoid the requirement of locking access
const uint8_t timer_preload_value = 131; // 256 - 131 = 125 ticks @ 8MHz/64 = 1ms
ISR(TIM1_OVF_vect){
  TCNT1 = timer_preload_value;
  
  if(led_on_duration_ms > 0){
    led_on_duration_ms--; 
  }
 
  if(check_for_pet_timer_ms > 0){
    check_for_pet_timer_ms--; 
  }    
  
  if(once_per_millisecond_timer_ms > 0){
    once_per_millisecond_timer_ms--; 
  }  

  if(setup_timeout_timer_ms > 0){
    setup_timeout_timer_ms--;
  }

  if(internal_watchdog_timeout_timer_ms > 0){
    internal_watchdog_timeout_timer_ms--;
  }
}

void perform_reset_sequence(void);
void blinkLedFast(uint8_t n);

void setup(){
  wdt_enable(WDTO_500MS);
 
  TCCR1 = 0x07; // divide by 1024
  TCNT1 = timer_preload_value;
  TIMSK = _BV(TOIE1);    

  pinMode(host_reset_pin, INPUT);
  pinMode(pet_input_pin, INPUT_PULLUP);
  pinMode(led_pin, OUTPUT);
  pinMode(debug_pin, OUTPUT);
  digitalWrite(debug_pin, debug_state);
  digitalWrite(led_pin, LOW);

  blinkLedFast(2);
 
  for(;;){
    if (setup_timeout_timer_ms == 0){
      break;
    }

    handleDebug();    
    handleOncePerMillisecond();
    
    // handle what happens the host is reset by something else
    if(digitalRead(host_reset_pin) == 0){
      while(digitalRead(host_reset_pin) == 0){
        continue; 
      }
      soft_reset();
    }       
  }
 
  blinkLedFast(1);    
  
}

void loop(){      
  // handle what happens when the host is reset by something else
  if(digitalRead(host_reset_pin) == 0){
    while(digitalRead(host_reset_pin) == 0){
      continue; 
    }
    soft_reset();
  }

  handleDebug();
  
  // the once per millisecond_timer_ms task runs to
  // increment a counter every millisecond, regardless
  // of other activity, and the counter can be cleared by 
  // the check_for_pet_timer_ms task
  handleOncePerMillisecond();

  // the check_for_pet_timer_ms task runs to 
  // 1 - determine if the watchdog is currently being pet, and restart the ms_without_being_pet if necessary
  // 2 - issue a reset if an early pet is detected
  // 3 - issue a reset if too long has passed without a pet
  if(check_for_pet_timer_ms == 0){
    check_for_pet_timer_ms = 1;
    
    // issue a reset if either: 
    // CONDITION #1: the input signal is LOW and the counter is lower than minimum_wait_period_after_petting_ms  
    if(digitalRead(pet_input_pin) == 0){ // CONDITION #1   
      if(ms_without_being_pet < min_wait_period_after_petting_ms){
        if(!first_pet){
          // the pet signal arrived too early (on the second, or later, pet)    
          perform_reset_sequence(); 
        }
      }
              
      // pet signal must be within the allowable window            
      ms_without_being_pet = 0; // timing the new window starts from now     
      led_on_duration_ms = LED_SHORT_BLINK_DURATION_MS; // turn the LED on for 50ms      
      check_for_pet_timer_ms = PET_WATCHDOG_RELEASE_DURATION_MS; // ignore currently being pet for 5ms          
      first_pet = false; // by definition, it's no longer the first pet         
    }
    // or CONDITION #2: the counter is higher than maximum_wait_period_after_petting and not currently being pet
    else{ // if(digitalRead(pet_input_pin) == 1)
      if(ms_without_being_pet >= maximum_wait_period_after_petting_ms){ 
        // the pet signal arrived too late      
        perform_reset_sequence(); 
      }
    }   
  }
  
  // manage the LED
  if(led_on_duration_ms > 0){
    digitalWrite(led_pin, HIGH);   //turn on the LED  
  }
  else{
    digitalWrite(led_pin, LOW);    //turn off the LED    
  }
}

void perform_reset_sequence(void){
  // blink the LED fast three times
  blinkLedFast(3);
    
  pinMode(host_reset_pin, OUTPUT); 
  digitalWrite(host_reset_pin, LOW);

  delayMicroseconds(10);     // minimum pulse width on reset is 2.5us according ot the datasheet
                             // so 10us should be plenty
  
  pinMode(host_reset_pin, INPUT);

  soft_reset();              // reset the watchdog timer so the dance can begin again  
                             // this is *really* important or the host will try and send a serial message
                             // that will be interpretted as random petting behavior and result in a
                             // vicious cycle of restarts
}

void blinkLedFast(uint8_t n){
  uint8_t ii = 0;
  for(ii = 0; ii < n; ii++){
    digitalWrite(led_pin, HIGH); // led on
    delay(50);                   // wait on 
    digitalWrite(led_pin, LOW);  // led off
    delay(150);                  // wait off
  } 
}

void handleDebug(void){
  if(debug_transition_timer_ms >= debug_transition_reload_ms){        
    debug_transition_timer_ms = 0;    
    
    debug_state = 1 - debug_state;
    if(debug_state){
      PORTB |= _BV(PB4);
    }
    else{
      PORTB &= ~_BV(PB4);
    }

    wdt_reset();
  }
}

void handleOncePerMillisecond(void){
  if(once_per_millisecond_timer_ms == 0) {
    once_per_millisecond_timer_ms = 1;    
    
    // increase the 16-bit counters
    debug_transition_timer_ms++; 
    ms_without_being_pet++; 
  }  
}

