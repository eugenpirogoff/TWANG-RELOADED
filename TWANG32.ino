/*
	TWANG32 - An ESP32 port of TWANG
	(c) B. Dring 3/2018
	License: Creative Commons 4.0 Attribution - Share Alike	
	
	TWANG was originally created by Critters
	https://github.com/Critters/TWANG
	
	It was inspired by Robin Baumgarten's Line Wobbler Game_Audio	

*/

#define VERSION "2018-03-21"

#include <FastLED.h>
#include<Wire.h>
#include "Arduino.h"
#include "RunningMedian.h"

// twang files
#include "twang_mpu.h"
#include "Enemy.h"
#include "Particle.h"
#include "Spawner.h"
#include "Lava.h"
#include "Boss.h"
#include "Conveyor.h"
#include "iSin.h"
#include "sound.h"
#include "settings.h"
#include "wifi_ap.h"
//#include "SoundData.h";
//#include "Game_Audio.h"


#define DATA_PIN        16
#define CLOCK_PIN       17
#define LED_TYPE        APA102
//#define LED_COLOR_ORDER BGR
#define NUM_LEDS        144

// what type of LED Strip....pick one
#define USE_APA102
//#define USE_NEOPIXEL

#ifdef USE_APA102
  #define LED_TYPE        APA102
  #define LED_COLOR_ORDER      BGR // typically this will be the order, but switch it if not
  #define CONVEYOR_BRIGHTNES 8
  #define LAVA_OFF_BRIGHTNESS 4
#endif

#ifdef USE_NEOPIXEL
	#define CONVEYOR_BRIGHTNES 40  // low neopixel values are nearly off, they need a higher value
	#define LAVA_OFF_BRIGHTNESS 15
#endif



//#define BRIGHTNESS           150
#define DIRECTION            1
#define MIN_REDRAW_INTERVAL  16    // Min redraw interval (ms) 33 = 30fps / 16 = 63fps
#define USE_GRAVITY          0     // 0/1 use gravity (LED strip going up wall)
#define BEND_POINT           550   // 0/1000 point at which the LED strip goes up the wall

// GAME
long previousMillis = 0;           // Time of the last redraw
int levelNumber = 0;
long lastInputTime = 0;
#define TIMEOUT              30000  // time until screen saver



int joystickTilt = 0;              // Stores the angle of the joystick
int joystickWobble = 0;            // Stores the max amount of acceleration (wobble)

// WOBBLE ATTACK
#define DEFAULT_ATTACK_WIDTH 70  // Width of the wobble attack, world is 1000 wide
int attack_width = DEFAULT_ATTACK_WIDTH;     
#define ATTACK_DURATION     500    // Duration of a wobble attack (ms)
long attackMillis = 0;             // Time the attack started
bool attacking = 0;                // Is the attack in progress?
#define BOSS_WIDTH          40

// TODO all animation durations should be defined rather than literals 
// because they are used in main loop and some sounds too.
#define STARTUP_WIPEUP_DUR 200
#define STARTUP_SPARKLE_DUR 1300
#define STARTUP_FADE_DUR 1500

#define GAMEOVER_SPREAD_DURATION 1000
#define GAMEOVER_FADE_DURATION 1500

#define WIN_FILL_DURATION 500     // sound has a freq effect that might need to be adjusted
#define WIN_CLEAR_DURATION 1000
#define WIN_OFF_DURATION 1200

Twang_MPU accelgyro = Twang_MPU();
CRGB leds[NUM_LEDS];
RunningMedian MPUAngleSamples = RunningMedian(5);
RunningMedian MPUWobbleSamples = RunningMedian(5);
iSin isin = iSin();

//#define DEBUG  // comment out to stop serial debugging

// POOLS
#define LIFE_LEDS 3
int lifeLEDs[LIFE_LEDS] = {7, 6, 5}; // these numbers are Arduino GPIO numbers...this is not used in the B. Dring enclosure design

#define ENEMY_COUNT 10
Enemy enemyPool[ENEMY_COUNT] = {
    Enemy(), Enemy(), Enemy(), Enemy(), Enemy(), Enemy(), Enemy(), Enemy(), Enemy(), Enemy()
};


#define PARTICLE_COUNT 40
Particle particlePool[PARTICLE_COUNT] = {
    Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle()
};

#define SPAWN_COUNT 5
Spawner spawnPool[SPAWN_COUNT] = {
    Spawner(), Spawner()
};

#define LAVA_COUNT 5
Lava lavaPool[LAVA_COUNT] = {
    Lava(), Lava(), Lava(), Lava()
};

#define CONVEYOR_COUNT 2
Conveyor conveyorPool[CONVEYOR_COUNT] = {
    Conveyor(), Conveyor()
};

Boss boss = Boss();

enum stages {
	STARTUP,
	PLAY,
	WIN,
	DEAD,
	SCREENSAVER,
	BOSS_KILLED,
	GAMEOVER
} stage;

long stageStartTime;               // Stores the time the stage changed for stages that are time based
int playerPosition;                // Stores the player position
int playerPositionModifier;        // +/- adjustment to player position
bool playerAlive;
long killTime;
int lives = LIVES_PER_LEVEL; 
bool lastLevel = false;

int score = 0;


void setup() {
	Serial.begin(115200);
	Serial.print("\r\nTWANG32 VERSION: "); Serial.println(VERSION);
	
	settings_init();	
	
	Wire.begin();
	accelgyro.initialize();

	FastLED.addLeds<LED_TYPE, DATA_PIN, CLOCK_PIN, LED_COLOR_ORDER>(leds, NUM_LEDS);
	FastLED.setBrightness(user_settings.led_brightness);
	FastLED.setDither(1);

	sound_init(DAC_AUDIO_PIN);
	
	ap_setup();		

	stage = STARTUP;
	stageStartTime = millis();
	lives = user_settings.lives_per_level;
}

void loop() {
  long mm = millis();
  int brightness = 0;
  
    
  ap_client_check(); // check for web client
  checkSerialInput();

  if(stage == PLAY){
      if(attacking){
          SFXattacking();
      }else{
          SFXtilt(joystickTilt);
					
					
      }
  }else if(stage == DEAD){
      SFXdead();
  }

  if (mm - previousMillis >= MIN_REDRAW_INTERVAL) {
      getInput(); 
      
      long frameTimer = mm;
      previousMillis = mm;

      if(abs(joystickTilt) > user_settings.joystick_deadzone){
            lastInputTime = mm;
            if(stage == SCREENSAVER){
                levelNumber = -1;
                stageStartTime = mm;
                stage = WIN;
            }
        }else{
            if(lastInputTime+TIMEOUT < mm){
        
                stage = SCREENSAVER;
            }
        }
        if(stage == SCREENSAVER){
            screenSaverTick();    
        }else if(stage == STARTUP){
			if (stageStartTime+STARTUP_FADE_DUR > mm) {
				tickStartup(mm);
			}
			else
			{
			SFXcomplete();
			levelNumber = 0;
			loadLevel();
			}
		}else if(stage == PLAY){
            // PLAYING
            if(attacking && attackMillis+ATTACK_DURATION < mm) attacking = 0;

            // If not attacking, check if they should be
            if(!attacking && joystickWobble > user_settings.attack_threshold){
                attackMillis = mm;
                attacking = 1;
            }

            // If still not attacking, move!
            playerPosition += playerPositionModifier;
            if(!attacking){
								SFXtilt(joystickTilt);
                int moveAmount = (joystickTilt/6.0);
                if(DIRECTION) moveAmount = -moveAmount;
                moveAmount = constrain(moveAmount, -MAX_PLAYER_SPEED, MAX_PLAYER_SPEED);
								
                playerPosition -= moveAmount;
                if(playerPosition < 0) 
					playerPosition = 0;
        
				// stop player from leaving if boss is alive
				if (boss.Alive() && playerPosition >= 1000) // move player back
					playerPosition = 999; //(NUM_LEDS - 1) * (1000.0/NUM_LEDS);
          
                if(playerPosition >= 1000 && !boss.Alive()) {
                    // Reached exit!
                    levelComplete();
                    return;
                }
            }

            if(inLava(playerPosition)){
                die();
            }

            // Ticks and draw calls
            FastLED.clear();
            tickConveyors();
            tickSpawners();
            tickBoss();
            tickLava();
            tickEnemies();
            drawPlayer();      
            drawAttack();
            drawExit();
        }else if(stage == DEAD){
            // DEAD
            FastLED.clear();
			tickDie(mm);
            if(!tickParticles()){
                loadLevel();
            }
        }else if(stage == WIN){
            // LEVEL COMPLETE     
            tickWin(mm);
        }else if(stage == BOSS_KILLED){
			tickBossKilled(mm);
			//tickComplete(mm);            
         } else if (stage == GAMEOVER) {
			  if (stageStartTime+GAMEOVER_FADE_DURATION > mm)
			  {				 
				tickGameover(mm);
			  }
			  else
			  {
				FastLED.clear(); 
				save_game_stats(false);				
				//score = 0; // reset the score
				levelNumber = 0;
				lives = user_settings.lives_per_level;
				loadLevel();
			  }          
         }

      FastLED.show();

  }

  
}

// ---------------------------------
// ------------ LEVELS -------------
// ---------------------------------
void loadLevel(){    	
	// leave these alone
	updateLives();
    cleanupLevel();    
    playerAlive = 1;
	lastLevel = false; // this gets changed on the boss level
	
	/// Defaults...OK to change the following items in the levels below
	attack_width = DEFAULT_ATTACK_WIDTH; 
	playerPosition = 0; 
	
	/* ==== Level Editing Guide ===============
	Level creation is done by adding to or editing the switch statement below
	
	You can add as many levels as you want but you must have a "case"  
	for each level. The case numbers must start at 0 and go up without skipping any numbers.
	
	Don't edit case 0 or the last (boss) level. They are special cases and editing them could
	break the code.
	
	TWANG uses a virtual 1000 LED grid. It will then scale that number to your strip, so if you
	want something in the middle of your strip use the number 500. Consider the size of your strip
	when adding features. All time values are specified in milliseconds (1/1000 of a second)
	
	You can add any of the following features.
	
	Enemies: You add up to 10 enemies with the spawnEnemy(...) functions.
		spawnEnemy(position, direction, speed, wobble);
			position: Where the enemy starts 
			direction: If it moves, what direction does it go 0=down, 1=away
			speed: How fast does it move. Typically 1 to 4.
			wobble: 0=regular movement, 1=bouncing back and forth use the speed value
				to set the length of the wobble.
				
	Spawn Pools: This generates and endless source of new enemies. 2 pools max
		spawnPool[index].Spawn(position, rate, speed, direction, activate);
			index: You can have up to 2 pools, use an index of 0 for the first and 1 for the second.
			position: The location the enemies with be generated from. 
			rate: The time in milliseconds between each new enemy
			speed: How fast they move. Typically 1 to 4.
			direction: Directions they go 0=down, 1=away
			activate: The delay in milliseconds before the first enemy
			
	Lava: You can create 4 pools of lava.
		spawnLava(left, right, ontime, offtime, offset, state);
			left: the lower end of the lava pool
			right: the upper end of the lava pool
			ontime: How long the lave stays on.
			offset: the delay before the first switch
			state: does it start on or off
	
	Conveyor: You can create 2 conveyors.
		spawnConveyor(startPoint, endPoint, direction)
			startPoint: The close end of the conveyor
			endPoint: The far end of the conveyor
			direction(speed): positive = away, negative = towards you (must be less than +/- player speed)
	
	===== Other things you can adjust per level ================ 
	
		Player Start position:
			playerPosition = xxx; 
				
			
		The size of the TWANG attack
			attack_width = xxx;
					
	
	*/
    switch(levelNumber){
        case 0: // basic introduction 
            playerPosition = 200;			
            spawnEnemy(1, 0, 0, 0);					
            break;
        case 1:
            // Slow moving enemy			
            spawnEnemy(900, 0, 1, 0);			
            break;
        case 2:
            // Spawning enemies at exit every 2 seconds
            spawnPool[0].Spawn(1000, 3000, 2, 0, 0);
            break;
        case 3:
            // Lava intro
            spawnLava(400, 490, 2000, 2000, 0, Lava::OFF);
			spawnEnemy(350, 0, 1, 0);
            spawnPool[0].Spawn(1000, 5500, 3, 0, 0);
			
            break;
        case 4:
            // Sin enemy
            spawnEnemy(700, 1, 7, 275);
            spawnEnemy(500, 1, 5, 250);
            break;
		case 5:
			// Sin enemy swarm
            spawnEnemy(700, 1, 7, 275);
            spawnEnemy(500, 1, 5, 250);
			
			spawnEnemy(600, 1, 7, 200);
            spawnEnemy(800, 1, 5, 350);
			
			spawnEnemy(400, 1, 7, 150);
            spawnEnemy(450, 1, 5, 400);
			
            break;
        case 6:
            // Conveyor
            spawnConveyor(100, 600, -6);
            spawnEnemy(800, 0, 0, 0);
            break;
        case 7:
            // Conveyor of enemies
            spawnConveyor(50, 1000, 6);
            spawnEnemy(300, 0, 0, 0);
            spawnEnemy(400, 0, 0, 0);
            spawnEnemy(500, 0, 0, 0);
            spawnEnemy(600, 0, 0, 0);
            spawnEnemy(700, 0, 0, 0);
            spawnEnemy(800, 0, 0, 0);
            spawnEnemy(900, 0, 0, 0);
            break;
		case 8:   // spawn train;		
			spawnPool[0].Spawn(900, 1000, 3, 0, 0);					
			break;
		case 9:   // spawn train skinny width;
			attack_width = 32;
			spawnPool[0].Spawn(900, 1800, 2, 0, 0);
			break;
		case 10:  // evil fast split spawner
			spawnPool[0].Spawn(550, 1100, 3, 0, 0);
			spawnPool[1].Spawn(550, 1100, 3, 1, 0);		
			break;
		case 11: // split spawner with exit blocking lava
			spawnPool[0].Spawn(500, 1100, 3, 0, 0);
			spawnPool[1].Spawn(500, 1100, 3, 1, 0);		
			spawnLava(900, 950, 2200, 800, 2000, Lava::OFF);
			break;
        case 12:
            // Lava run
            spawnLava(195, 300, 2000, 2000, 0, Lava::OFF);
            spawnLava(400, 500, 2000, 2000, 0, Lava::OFF);
            spawnLava(600, 700, 2000, 2000, 0, Lava::OFF);
            spawnPool[0].Spawn(1000, 3800, 4, 0, 0);
            break;
        case 13:
            // Sin enemy #2 practice (slow conveyor)
            spawnEnemy(700, 1, 7, 275);
            spawnEnemy(500, 1, 5, 250);
            spawnPool[0].Spawn(1000, 5500, 4, 0, 3000);
            spawnPool[1].Spawn(0, 5500, 5, 1, 10000);
            spawnConveyor(100, 900, -4);
            break;
		case 14:
			// Sin enemy #2 (fast conveyor)
            spawnEnemy(700, 1, 7, 275);
            spawnEnemy(500, 1, 5, 250);
            spawnPool[0].Spawn(1000, 5500, 4, 0, 3000);
            spawnPool[1].Spawn(0, 5500, 5, 1, 10000);
            spawnConveyor(100, 900, -6);
			break;
        case 15: // (don't edit last level)
            // Boss this should always be the last level			
            spawnBoss();
            break;
    }
    stageStartTime = millis();
    stage = PLAY;
}

void spawnBoss(){
	lastLevel = true;
    boss.Spawn();	
    moveBoss();
}

void moveBoss(){
    int spawnSpeed = 2500;
    if(boss._lives == 2) spawnSpeed = 2000;
    if(boss._lives == 1) spawnSpeed = 1500;
    spawnPool[0].Spawn(boss._pos, spawnSpeed, 3, 0, 0);
    spawnPool[1].Spawn(boss._pos, spawnSpeed, 3, 1, 0);
}

/* ======================== spawn Functions =====================================

   The following spawn functions add items to pools by looking for an unactive
   item in the pool. You can only add as many as the ..._COUNT. Additonal attemps 
   to add will be ignored.   
   
   ==============================================================================
*/
void spawnEnemy(int pos, int dir, int speed, int wobble){
    for(int e = 0; e<ENEMY_COUNT; e++){  // look for one that is not alive for a place to add one
        if(!enemyPool[e].Alive()){
            enemyPool[e].Spawn(pos, dir, speed, wobble);
            enemyPool[e].playerSide = pos > playerPosition?1:-1;
            return;
        }
    }
}

void spawnLava(int left, int right, int ontime, int offtime, int offset, int state){
    for(int i = 0; i<LAVA_COUNT; i++){
        if(!lavaPool[i].Alive()){
            lavaPool[i].Spawn(left, right, ontime, offtime, offset, state);
            return;
        }
    }
}

void spawnConveyor(int startPoint, int endPoint, int dir){
    for(int i = 0; i<CONVEYOR_COUNT; i++){
        if(!conveyorPool[i]._alive){
            conveyorPool[i].Spawn(startPoint, endPoint, dir);
            return;
        }
    }
}

void cleanupLevel(){
    for(int i = 0; i<ENEMY_COUNT; i++){
        enemyPool[i].Kill();
    }
    for(int i = 0; i<PARTICLE_COUNT; i++){
        particlePool[i].Kill();
    }
    for(int i = 0; i<SPAWN_COUNT; i++){
        spawnPool[i].Kill();
    }
    for(int i = 0; i<LAVA_COUNT; i++){
        lavaPool[i].Kill();
    }
    for(int i = 0; i<CONVEYOR_COUNT; i++){
        conveyorPool[i].Kill();
    }
    boss.Kill();
}

void levelComplete(){
    stageStartTime = millis();
    stage = WIN;
	
	if (lastLevel) {
		stage = BOSS_KILLED;
	}
	if (levelNumber != 0)  // no points for the first level
	{			
		score = score + (lives * 10);  // 
	}    
}

void nextLevel(){		
    levelNumber ++;
	
	if(lastLevel)		
		levelNumber = 0;
	
	lives = user_settings.lives_per_level;
    loadLevel();
}

void gameOver(){

    levelNumber = 0;
    loadLevel();
}

void die(){
    playerAlive = 0;
    if(levelNumber > 0) 
		lives --; 
	
    if(lives == 0){
       stage = GAMEOVER;
       stageStartTime = millis();
    }
    else
    {
      for(int p = 0; p < PARTICLE_COUNT; p++){
          particlePool[p].Spawn(playerPosition);
      }
      stageStartTime = millis();
      stage = DEAD;
    }
    killTime = millis();
}

// ----------------------------------
// -------- TICKS & RENDERS ---------
// ----------------------------------
void tickStartup(long mm)
{
  FastLED.clear();
  if(stageStartTime+STARTUP_WIPEUP_DUR > mm) // fill to the top with green
  {
    int n = _min(map(((mm-stageStartTime)), 0, STARTUP_WIPEUP_DUR, 0, NUM_LEDS), NUM_LEDS);  // fill from top to bottom
    for(int i = 0; i<= n; i++){     
      leds[i] = CRGB(0, 255, 0);
    }   
  }
  else if(stageStartTime+STARTUP_SPARKLE_DUR > mm) // sparkle the full green bar    
  {
    for(int i = 0; i< NUM_LEDS; i++){    
      if(random8(30) < 28)
        leds[i] = CRGB(0, 255, 0);  // most are green
      else {
        int flicker = random8(250);
        leds[i] = CRGB(flicker, 150, flicker); // some flicker brighter
      }
    }
    
  }
  else if (stageStartTime+STARTUP_FADE_DUR > mm) // fade it out to bottom
  {
    int n = _max(map(((mm-stageStartTime)), STARTUP_SPARKLE_DUR, STARTUP_FADE_DUR, 0, NUM_LEDS), 0);  // fill from top to bottom
    int brightness = _max(map(((mm-stageStartTime)), STARTUP_SPARKLE_DUR, STARTUP_FADE_DUR, 255, 0), 0);
    // for(int i = 0; i<= n; i++){    
       
      // leds[i] = CRGB(0, brightness, 0);
    // }
    for(int i = n; i< NUM_LEDS; i++){   
       
      leds[i] = CRGB(0, brightness, 0);
    }   
  } 
  SFXFreqSweepWarble(STARTUP_FADE_DUR, millis()-stageStartTime, 40, 400, 20);
  
}



void tickEnemies(){
    for(int i = 0; i<ENEMY_COUNT; i++){
        if(enemyPool[i].Alive()){
            enemyPool[i].Tick();
            // Hit attack?
            if(attacking){
                if(enemyPool[i]._pos > playerPosition-(attack_width/2) && enemyPool[i]._pos < playerPosition+(attack_width/2)){
                   enemyPool[i].Kill();
                   SFXkill();
                }
            }
            if(inLava(enemyPool[i]._pos)){
                enemyPool[i].Kill();
                SFXkill();
            }
            // Draw (if still alive)
            if(enemyPool[i].Alive()) {
                leds[getLED(enemyPool[i]._pos)] = CRGB(255, 0, 0);
            }
            // Hit player?
            if(
                (enemyPool[i].playerSide == 1 && enemyPool[i]._pos <= playerPosition) ||
                (enemyPool[i].playerSide == -1 && enemyPool[i]._pos >= playerPosition)
            ){
                die();
                return;
            }
        }
    }
}

void tickBoss(){
	// DRAW
	if(boss.Alive()){
		boss._ticks ++;
		for(int i = getLED(boss._pos-BOSS_WIDTH/2); i<=getLED(boss._pos+BOSS_WIDTH/2); i++){
			leds[i] = CRGB::DarkRed;
			leds[i] %= 100;
		}
		// CHECK COLLISION
		if(getLED(playerPosition) > getLED(boss._pos - BOSS_WIDTH/2) && getLED(playerPosition) < getLED(boss._pos + BOSS_WIDTH)){
			die();
			return;
		}
		// CHECK FOR ATTACK
		if(attacking){
			if(
					(getLED(playerPosition+(attack_width/2)) >= getLED(boss._pos - BOSS_WIDTH/2) && getLED(playerPosition+(attack_width/2)) <= getLED(boss._pos + BOSS_WIDTH/2)) ||
					(getLED(playerPosition-(attack_width/2)) <= getLED(boss._pos + BOSS_WIDTH/2) && getLED(playerPosition-(attack_width/2)) >= getLED(boss._pos - BOSS_WIDTH/2))
					){
				boss.Hit();
				if(boss.Alive()){
					moveBoss();
				}else{
					spawnPool[0].Kill();
					spawnPool[1].Kill();
				}
			}
		}
	}
}

void drawPlayer(){
    leds[getLED(playerPosition)] = CRGB(0, 255, 0);
}

void drawExit(){
    if(!boss.Alive()){
        leds[NUM_LEDS-1] = CRGB(0, 0, 255);
    }
}

void tickSpawners(){
    long mm = millis();
    for(int s = 0; s<SPAWN_COUNT; s++){
        if(spawnPool[s].Alive() && spawnPool[s]._activate < mm){
            if(spawnPool[s]._lastSpawned + spawnPool[s]._rate < mm || spawnPool[s]._lastSpawned == 0){
                spawnEnemy(spawnPool[s]._pos, spawnPool[s]._dir, spawnPool[s]._sp, 0);
                spawnPool[s]._lastSpawned = mm;
            }
        }
    }
}

void tickLava(){
	int A, B, p, i, brightness, flicker;
	long mm = millis();
	Lava LP;
	for(i = 0; i<LAVA_COUNT; i++){        
		LP = lavaPool[i];
		if(LP.Alive()){
			A = getLED(LP._left);
			B = getLED(LP._right);
			if(LP._state == Lava::OFF){
				if(LP._lastOn + LP._offtime < mm){
					LP._state = Lava::ON;
					LP._lastOn = mm;
				}
				for(p = A; p<= B; p++){
					flicker = random8(3);
					leds[p] = CRGB(3+flicker, (3+flicker)/1.5, 0);
				}
			}else if(LP._state == Lava::ON){
				if(LP._lastOn + LP._ontime < mm){
					LP._state = Lava::OFF;
					LP._lastOn = mm;
				}
				for(p = A; p<= B; p++){
					if(random8(30) < 29)
						leds[p] = CRGB(150, 0, 0);
					else
						leds[p] = CRGB(180, 100, 0);
				}
				//if (random8(30) > 3)
				//sound(380 + random8(200), MAX_VOLUME / 2); // scary lava noise
			}
		}
		lavaPool[i] = LP;
	}
}

bool tickParticles(){
	bool stillActive = false;
	uint8_t brightness;
	for(int p = 0; p < PARTICLE_COUNT; p++){
		if(particlePool[p].Alive()){
			particlePool[p].Tick(USE_GRAVITY);
			
			if (particlePool[p]._power < 5)
			{
				brightness = (5 - particlePool[p]._power) * 10;
				leds[getLED(particlePool[p]._pos)] += CRGB(brightness, brightness/2, brightness/2);\
			}
			else      
			leds[getLED(particlePool[p]._pos)] += CRGB(particlePool[p]._power, 0, 0);
			
			stillActive = true;
		}
	}
	return stillActive;
}

void tickConveyors(){    
	
	//TODO should the visual speed be proportional to the conveyor speed?
    
	int b, speed, n, i, ss, ee, led;
    long m = 10000+millis();
    playerPositionModifier = 0;
	
	int levels = 5; // brightness levels in conveyor 	
	

    for(i = 0; i<CONVEYOR_COUNT; i++){
        if(conveyorPool[i]._alive){
            speed = constrain(conveyorPool[i]._speed, -MAX_PLAYER_SPEED+1, MAX_PLAYER_SPEED-1);
            ss = getLED(conveyorPool[i]._startPoint);
            ee = getLED(conveyorPool[i]._endPoint);
            for(led = ss; led<ee; led++){
                
                n = (-led + (m/100)) % levels;
                if(speed < 0) 
					n = (led + (m/100)) % levels;
				
				b = map(n, 5, 0, 0, CONVEYOR_BRIGHTNES);
                if(b > 0) 
					leds[led] = CRGB(0, 0, b);
            }

            if(playerPosition > conveyorPool[i]._startPoint && playerPosition < conveyorPool[i]._endPoint){
                playerPositionModifier = speed;
            }
        }
    }
}

void tickComplete(long mm)  // the boss is dead
{
  int brightness = 0;
  FastLED.clear();
  SFXcomplete();
  if(stageStartTime+500 > mm){
    int n = _max(map(((mm-stageStartTime)), 0, 500, NUM_LEDS, 0), 0);
    for(int i = NUM_LEDS; i>= n; i--){
      brightness = (sin(((i*10)+mm)/500.0)+1)*255;
      leds[i].setHSV(brightness, 255, 50);
    }
  }else if(stageStartTime+5000 > mm){
    for(int i = NUM_LEDS; i>= 0; i--){
      brightness = (sin(((i*10)+mm)/500.0)+1)*255;
      leds[i].setHSV(brightness, 255, 50);
    }
  }else if(stageStartTime+5500 > mm){
    int n = _max(map(((mm-stageStartTime)), 5000, 5500, NUM_LEDS, 0), 0);
    for(int i = 0; i< n; i++){
      brightness = (sin(((i*10)+mm)/500.0)+1)*255;
      leds[i].setHSV(brightness, 255, 50);
    }
  }else{
    nextLevel();
  }
}

void tickBossKilled(long mm) // boss funeral
{
	int brightness = 0;
	FastLED.clear();	
	if(stageStartTime+500 > mm){
		int n = _max(map(((mm-stageStartTime)), 0, 500, NUM_LEDS, 0), 0);
		for(int i = NUM_LEDS; i>= n; i--){
			brightness = (sin(((i*10)+mm)/500.0)+1)*255;
			leds[i].setHSV(brightness, 255, 50);
		}
		SFXbosskilled();
	}else if(stageStartTime+6500 > mm){
		for(int i = NUM_LEDS; i>= 0; i--){
			brightness = (sin(((i*10)+mm)/500.0)+1)*255;
			leds[i].setHSV(brightness, 255, 50);
		}
		SFXbosskilled();
	}else if(stageStartTime+7000 > mm){
		int n = _max(map(((mm-stageStartTime)), 5000, 5500, NUM_LEDS, 0), 0);
		for(int i = 0; i< n; i++){
			brightness = (sin(((i*10)+mm)/500.0)+1)*255;
			leds[i].setHSV(brightness, 255, 50);
		}
		SFXcomplete();
	}else{		
		nextLevel();
	}
}

void tickDie(long mm) { // a short bright explosion...particles persist after it.
	const int duration = 100; // milliseconds
	const int width = 10;     // half width of the explosion

	if(stageStartTime+duration > mm) {// Spread red from player position up and down the width
	
		int brightness = map((mm-stageStartTime), 0, duration, 255, 50); // this allows a fade from white to red
		
		// fill up
		int n = _max(map(((mm-stageStartTime)), 0, duration, getLED(playerPosition), getLED(playerPosition)+width), 0);
		for(int i = getLED(playerPosition); i<= n; i++){
			leds[i] = CRGB(255, brightness, brightness);
		}
		
		// fill to down
		n = _max(map(((mm-stageStartTime)), 0, duration, getLED(playerPosition), getLED(playerPosition)-width), 0);
		for(int i = getLED(playerPosition); i>= n; i--){
			leds[i] = CRGB(255, brightness, brightness);
		}
	}
}

void tickGameover(long mm) {
  
    int brightness = 0;

  if(stageStartTime+GAMEOVER_SPREAD_DURATION > mm) // Spread red from player position to top and bottom
  {
    // fill to top
    int n = _max(map(((mm-stageStartTime)), 0, GAMEOVER_SPREAD_DURATION, getLED(playerPosition), NUM_LEDS), 0);
    for(int i = getLED(playerPosition); i<= n; i++){
      leds[i] = CRGB(255, 0, 0);
    }
    // fill to bottom
    n = _max(map(((mm-stageStartTime)), 0, GAMEOVER_SPREAD_DURATION, getLED(playerPosition), 0), 0);
    for(int i = getLED(playerPosition); i>= n; i--){
      leds[i] = CRGB(255, 0, 0);
    }
    SFXgameover();
  }
  else if(stageStartTime+GAMEOVER_FADE_DURATION > mm)  // fade down to bottom and fade brightness
  {
    int n = _max(map(((mm-stageStartTime)), GAMEOVER_FADE_DURATION, GAMEOVER_SPREAD_DURATION, NUM_LEDS, 0), 0);
    brightness =  map(((mm-stageStartTime)), GAMEOVER_SPREAD_DURATION, GAMEOVER_FADE_DURATION, 200, 0);

    for(int i = 0; i<= n; i++){
      leds[i] = CRGB(brightness, 0, 0);
    }
    SFXcomplete();
  }

}



void tickWin(long mm) {
  int brightness = 0;
  FastLED.clear();
  if(stageStartTime+WIN_FILL_DURATION > mm){
    int n = _max(map(((mm-stageStartTime)), 0, WIN_FILL_DURATION, NUM_LEDS, 0), 0);  // fill from top to bottom
    for(int i = NUM_LEDS; i>= n; i--){
      brightness = user_settings.led_brightness;
      leds[i] = CRGB(0, brightness, 0);
    }
    SFXwin();
  }else if(stageStartTime+WIN_CLEAR_DURATION > mm){
    int n = _max(map(((mm-stageStartTime)), WIN_FILL_DURATION, WIN_CLEAR_DURATION, NUM_LEDS, 0), 0);  // clear from top to bottom
    for(int i = 0; i< n; i++){
      brightness = user_settings.led_brightness; 
      leds[i] = CRGB(0, brightness, 0);
    }
    SFXwin();
  }else if(stageStartTime+WIN_OFF_DURATION > mm){   // wait a while with leds off
    leds[0] = CRGB(0, user_settings.led_brightness, 0);
  }else{        
    nextLevel();
  }
}


void drawLives()
{
  // show how many lives are left by drawing a short line of green leds for each life
  SFXcomplete();  // stop any sounds
  FastLED.clear(); 
  
  int pos = 0;  
  for (int i = 0; i < lives; i++)
  { 
      for (int j=0; j<4; j++)
      {
        leds[pos++] = CRGB(0, 255, 0);
        FastLED.show();       
      }
      leds[pos++] = CRGB(0, 0, 0);      
      delay(20);
  }
  FastLED.show();
  delay(400);
  FastLED.clear();
}



void drawAttack(){
    if(!attacking) return;
    int n = map(millis() - attackMillis, 0, ATTACK_DURATION, 100, 5);
    for(int i = getLED(playerPosition-(attack_width/2))+1; i<=getLED(playerPosition+(attack_width/2))-1; i++){
        leds[i] = CRGB(0, 0, n);
    }
    if(n > 90) {
        n = 255;
        leds[getLED(playerPosition)] = CRGB(255, 255, 255);
    }else{
        n = 0;
        leds[getLED(playerPosition)] = CRGB(0, 255, 0);
    }
    leds[getLED(playerPosition-(attack_width/2))] = CRGB(n, n, 255);
    leds[getLED(playerPosition+(attack_width/2))] = CRGB(n, n, 255);
}

int getLED(int pos){
    // The world is 1000 pixels wide, this converts world units into an LED number
    return constrain((int)map(pos, 0, 1000, 0, NUM_LEDS-1), 0, NUM_LEDS-1);
}

bool inLava(int pos){
    // Returns if the player is in active lava
    int i;
    Lava LP;
    for(i = 0; i<LAVA_COUNT; i++){
        LP = lavaPool[i];
        if(LP.Alive() && LP._state == Lava::ON){
            if(LP._left < pos && LP._right > pos) return true;
        }
    }
    return false;
}

void updateLives(){
    // Updates the life LEDs to show how many lives the player has left
    //for(int i = 0; i<LIFE_LEDS; i++){
    //   digitalWrite(lifeLEDs[i], lives>i?HIGH:LOW);
    //}
	
	drawLives();
}

void save_game_stats(bool bossKill)
{	
	user_settings.games_played += 1;
	user_settings.total_points += score;
	if (score > user_settings.high_score)
		user_settings.high_score += score;
	if (bossKill)
		user_settings.boss_kills += 1;
	
	show_game_stats();	
	settings_eeprom_write();
}

// ---------------------------------
// --------- SCREENSAVER -----------
// ---------------------------------
void screenSaverTick(){
    int n, b, c, i;
    long mm = millis();
    int mode = (mm/30000)%5;
  
  SFXcomplete(); // make sure there is not sound...play testing showed this to be a problem

    for(i = 0; i<NUM_LEDS; i++){
        leds[i].nscale8(250);
    }
    if(mode == 0){
        // Marching green <> orange
        n = (mm/250)%10;
        b = 10+((sin(mm/500.00)+1)*20.00);
        c = 20+((sin(mm/5000.00)+1)*33);
        for(i = 0; i<NUM_LEDS; i++){
            if(i%10 == n){
                leds[i] = CHSV( c, 255, 150);
            }
        }
    }else if(mode >= 1){
        // Random flashes
        randomSeed(mm);
        for(i = 0; i<NUM_LEDS; i++){
            if(random8(20) == 0){
                leds[i] = CHSV( 25, 255, 100);
            }
        }
    }
}

// ---------------------------------
// ----------- JOYSTICK ------------
// ---------------------------------
void getInput(){
    // This is responsible for the player movement speed and attacking.
    // You can replace it with anything you want that passes a -90>+90 value to joystickTilt
    // and any value to joystickWobble that is greater than ATTACK_THRESHOLD (defined at start)
    // For example you could use 3 momentary buttons:
        // if(digitalRead(leftButtonPinNumber) == HIGH) joystickTilt = -90;
        // if(digitalRead(rightButtonPinNumber) == HIGH) joystickTilt = 90;
        // if(digitalRead(attackButtonPinNumber) == HIGH) joystickWobble = ATTACK_THRESHOLD;
	int16_t ax, ay, az;
	int16_t gx, gy, gz;

    accelgyro.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
    int a = (JOYSTICK_ORIENTATION == 0?ax:(JOYSTICK_ORIENTATION == 1?ay:az))/166;
    int g = (JOYSTICK_ORIENTATION == 0?gx:(JOYSTICK_ORIENTATION == 1?gy:gz));
	
    if(abs(a) < user_settings.joystick_deadzone) a = 0;
    if(a > 0) a -= user_settings.joystick_deadzone;
    if(a < 0) a += user_settings.joystick_deadzone;
    MPUAngleSamples.add(a);
    MPUWobbleSamples.add(g);

    joystickTilt = MPUAngleSamples.getMedian();
    if(JOYSTICK_DIRECTION == 1) {
        joystickTilt = 0-joystickTilt;
    }
    joystickWobble = abs(MPUWobbleSamples.getHighest());
}

// ---------------------------------
// -------------- SFX --------------
// ---------------------------------

/*
/*
   This is used sweep across (up or down) a frequency range for a specified duration.
   A sin based warble is added to the frequency. This function is meant to be called
   on each frame to adjust the frequency in sync with an animation
   
   duration 	= over what time period is this mapped
   elapsedTime 	= how far into the duration are we in
   freqStart 	= the beginning frequency
   freqEnd 		= the ending frequency
   warble 		= the amount of warble added (0 disables)   
   

*/
void SFXFreqSweepWarble(int duration, int elapsedTime, int freqStart, int freqEnd, int warble)
{
	int freq = map_constrain(elapsedTime, 0, duration, freqStart, freqEnd);
	if (warble)
			warble = map(sin(millis()/20.0)*1000.0, -1000, 1000, 0, warble);
		
	sound(freq + warble, user_settings.audio_volume);
}

/*
   
   This is used sweep across (up or down) a frequency range for a specified duration.
   Random noise is optionally added to the frequency. This function is meant to be called
   on each frame to adjust the frequency in sync with an animation
   
   duration 	= over what time period is this mapped
   elapsedTime 	= how far into the duration are we in
   freqStart 	= the beginning frequency
   freqEnd 		= the ending frequency
   noiseFactor 	= the amount of noise to added/subtracted (0 disables)   
   

*/
void SFXFreqSweepNoise(int duration, int elapsedTime, int freqStart, int freqEnd, uint8_t noiseFactor)
{
	int freq;
	
	if (elapsedTime > duration)
		freq = freqEnd;
	else
		freq = map(elapsedTime, 0, duration, freqStart, freqEnd);
	
	if (noiseFactor)
			noiseFactor = noiseFactor - random8(noiseFactor / 2);
		
	sound(freq + noiseFactor, user_settings.audio_volume);
}


void SFXtilt(int amount){
	if  (amount == 0){
		SFXcomplete();
		return;	
	}
	
    int f = map(abs(amount), 0, 90, 80, 900)+random8(100);
    if(playerPositionModifier < 0) f -= 500;
    if(playerPositionModifier > 0) f += 200;		
		int vol = map(abs(amount), 0, 90, user_settings.audio_volume / 2, user_settings.audio_volume * 3/4);
    sound(f,vol);
}
void SFXattacking(){
    int freq = map(sin(millis()/2.0)*1000.0, -1000, 1000, 500, 600);
    if(random8(5)== 0){
      freq *= 3;
    }
    sound(freq, user_settings.audio_volume);
}
void SFXdead(){	
	SFXFreqSweepNoise(1000, millis()-killTime, 1000, 10, 200);
}

void SFXgameover(){
	SFXFreqSweepWarble(GAMEOVER_SPREAD_DURATION, millis()-killTime, 440, 20, 60);
}

void SFXkill(){
    sound(2000, user_settings.audio_volume);
}
void SFXwin(){
	SFXFreqSweepWarble(WIN_OFF_DURATION, millis()-stageStartTime, 40, 400, 20);
}

void SFXbosskilled()
{
	SFXFreqSweepWarble(7000, millis()-stageStartTime, 75, 1100, 60);
}

void SFXcomplete(){
    soundOff();
}


/*
  This works just like the map function except x is constrained to the range of in_min and in_max
*/
long map_constrain(long x, long in_min, long in_max, long out_min, long out_max)
{
  // constain the x value to be between in_min and in_max
  if (in_max > in_min){   // map allows min to be larger than max, but constrain does not
    x = constrain(x, in_min, in_max);
  }
  else {
    x = constrain(x, in_max, in_min);
  }  
  return map(x, in_min, in_max, out_min, out_max);
}

