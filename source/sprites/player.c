#include "source/neslib_asm/neslib.h"
#include "source/sprites/player.h"
#include "source/library/bank_helpers.h"
#include "source/globals.h"
#include "source/map/map.h"
#include "source/configuration/game_states.h"
#include "source/configuration/system_constants.h"
#include "source/sprites/collision.h"
#include "source/sprites/sprite_definitions.h"
#include "source/sprites/map_sprites.h"
#include "source/menus/error.h"
#include "source/graphics/hud.h"
#include "source/graphics/game_text.h"
#include "source/configuration/game_info.h"
#include "source/configuration/player_info.h"
#include "temp/sprite_groups.h"

// Some useful global variables
ZEROPAGE_DEF(int, playerXPosition);
ZEROPAGE_DEF(int, playerYPosition);
ZEROPAGE_DEF(int, playerXVelocity);
ZEROPAGE_DEF(int, playerYVelocity);
ZEROPAGE_DEF(unsigned char, playerControlsLockTime);
ZEROPAGE_DEF(unsigned char, playerInvulnerabilityTime);
ZEROPAGE_DEF(unsigned char, playerDirection);
ZEROPAGE_DEF(unsigned char, teleportCooldownTime);
unsigned char teleportDestinationPointer;

// Huge pile of temporary variables
#define rawXPosition tempChar1
#define rawYPosition tempChar2
#define rawTileId tempChar3
#define collisionTempX tempChar4
#define collisionTempY tempChar5
#define collisionTempXRight tempChar6
#define collisionTempYBottom tempChar7

#define tempSpriteCollisionX tempInt1
#define tempSpriteCollisionY tempInt2

#define collisionTempXInt tempInt3
#define collisionTempYInt tempInt4

#define textBankNum tempChar8
#define ptrTextData tempInt1
#define spritePersistanceLocation tempInt1

void lookup_ptr_data() {
    bank_push(currentWorldId);

    ptrTextData = (unsigned int)map_0_text_lookup_address;
    ptrTextData += 
        (currentMap[MAP_DATA_EXTRA_START + ((lastPlayerSpriteCollisionId<<2)+1)]) +
        ((currentMap[MAP_DATA_EXTRA_START + ((lastPlayerSpriteCollisionId<<2)+2)]) << 8);
    textBankNum = currentMap[MAP_DATA_EXTRA_START + ((lastPlayerSpriteCollisionId<<2))];
    
    bank_pop();
}

CODE_BANK(PRG_BANK_PLAYER_SPRITE);


// NOTE: This uses tempChar1 through tempChar3; the caller must not use these.
void update_player_sprite(void) {
    // Calculate the position of the player itself, then use these variables to build it up with 4 8x8 NES sprites.
    rawXPosition = (playerXPosition >> PLAYER_POSITION_SHIFT);
    rawYPosition = (playerYPosition >> PLAYER_POSITION_SHIFT);
    rawTileId = PLAYER_SPRITE_TILE_ID + playerDirection;

    // Clamp the player's sprite X Position to 0 to make sure we don't loop over, even if technically we have.
    if (rawXPosition > (SCREEN_EDGE_RIGHT + 4)) {
        rawXPosition = 0;
    }
    
    if (playerInvulnerabilityTime && frameCount & PLAYER_INVULNERABILITY_BLINK_MASK) {
        // If the player is invulnerable, we hide their sprite about half the time to do a flicker animation.
        oam_spr(SPRITE_OFFSCREEN, SPRITE_OFFSCREEN, rawTileId, 0x00, PLAYER_SPRITE_INDEX);
        oam_spr(SPRITE_OFFSCREEN, SPRITE_OFFSCREEN, rawTileId + 1, 0x00, PLAYER_SPRITE_INDEX+4);
        oam_spr(SPRITE_OFFSCREEN, SPRITE_OFFSCREEN, rawTileId + 16, 0x00, PLAYER_SPRITE_INDEX+8);
        oam_spr(SPRITE_OFFSCREEN, SPRITE_OFFSCREEN, rawTileId + 17, 0x00, PLAYER_SPRITE_INDEX+12);

    } else {
        #if RI_PLAYER_MOVEMENT_TYPE_DEF == PLAYER_MOVEMENT_TYPE_2FRAME
            if (playerXVelocity != 0 || playerYVelocity != 0) {
                // Does some math with the current NES frame to add either 2 or 0 to the tile id, animating the sprite.
                rawTileId += ((frameCount >> SPRITE_ANIMATION_SPEED_DIVISOR) & 0x01) << 1;
            }

            oam_spr(rawXPosition, rawYPosition, rawTileId, 0x00, PLAYER_SPRITE_INDEX);
            oam_spr(rawXPosition + NES_SPRITE_WIDTH, rawYPosition, rawTileId + 1, 0x00, PLAYER_SPRITE_INDEX+4);
            oam_spr(rawXPosition, rawYPosition + NES_SPRITE_HEIGHT, rawTileId + 16, 0x00, PLAYER_SPRITE_INDEX+8);
            oam_spr(rawXPosition + NES_SPRITE_WIDTH, rawYPosition + NES_SPRITE_HEIGHT, rawTileId + 17, 0x00, PLAYER_SPRITE_INDEX+12);
        #elif RI_PLAYER_MOVEMENT_TYPE_DEF == PLAYER_MOVEMENT_TYPE_3FRAME
            oam_spr(rawXPosition, rawYPosition, rawTileId, 0x00, PLAYER_SPRITE_INDEX);
            oam_spr(rawXPosition + NES_SPRITE_WIDTH, rawYPosition, rawTileId + 1, 0x00, PLAYER_SPRITE_INDEX+4);
            if (playerXVelocity != 0 || playerYVelocity != 0) {
                rawTileId += 2;
                // Does some math with the current NES frame to add either 16 or 0 to the tile id, animating the sprite.
                rawTileId += ((frameCount >> SPRITE_ANIMATION_SPEED_DIVISOR) & 0x01) << 4;
            } else {
                rawTileId += 16;
            }

            oam_spr(rawXPosition, rawYPosition + NES_SPRITE_HEIGHT, rawTileId, 0x00, PLAYER_SPRITE_INDEX+8);
            oam_spr(rawXPosition + NES_SPRITE_WIDTH, rawYPosition + NES_SPRITE_HEIGHT, rawTileId + 1, 0x00, PLAYER_SPRITE_INDEX+12);
        #else
            // This tells you that RI_PLAYER_MOVEMENT_TYPE_DEF isn't defined by one of the above cases.
            unknown_player_movement_type_error();
        #endif

    }

}

// Tests the controller for a bunch of debug situations.
// Returns 1 if we need to exit the method immediately (not handle input this frame), false otherwise.
unsigned char test_debug_input(void) {
    if (controllerState & PAD_SELECT) {
        if (controllerState & PAD_START && !(lastControllerState & PAD_START)) {
            gameState = GAME_STATE_CREDITS;
            return 1;
        } else if (controllerState & PAD_LEFT && !(lastControllerState & PAD_LEFT)) {
            if (playerKeyCount > 0) {
                --playerKeyCount;
            }
        } else if (controllerState & PAD_RIGHT && !(lastControllerState & PAD_RIGHT)) {
            if (playerKeyCount < MAX_KEY_COUNT) {
                ++playerKeyCount;
            }
        } else if (controllerState & PAD_UP && !(lastControllerState & PAD_UP)) {
            if (playerHealth < playerMaxHealth) {
                ++playerHealth;
                playerMoneyCount++;
            }
        } else if (controllerState & PAD_DOWN && !(lastControllerState & PAD_DOWN)) {
            if (playerHealth > 1) {
                --playerHealth;
            }
        } else if (controllerState & PAD_A && !(lastControllerState & PAD_A)) {
            playerMoneyCount += 5;
            if ((playerMoneyCount & 0x0f) > 0x09) {
                playerMoneyCount += 6;
            }
            if ((playerMoneyCount & 0xf0) > 0x90) {
                playerMoneyCount += 0x60;
            }

            if (playerMoneyCount > 0x999) {
                playerMoneyCount = 0x999;
            }
        } else if (controllerState & PAD_B && !(lastControllerState & PAD_B)) {
            playerMoneyCount -= 5;
            if ((playerMoneyCount & 0x0f) > 0x09) {
                playerMoneyCount -= 6;
            }
            if ((playerMoneyCount & 0xf0) > 0x90) {
                playerMoneyCount -= 0x60;
            }
            // In this case we probably underflowed, since it's unsigned.
            if (playerMoneyCount > 0x999) {
                playerMoneyCount = 0;
            }
        }
    }

    return 0;
}

void handle_player_movement(void) {
    // Using a variable, so we can change the velocity based on pressing a button, having a special item,
    // or whatever you like!
    int maxVelocity = PLAYER_MAX_VELOCITY;
    lastControllerState = controllerState;
    controllerState = pad_poll(0);

    #if RI_DEBUG_MODE_BOOL
        if (test_debug_input()) {
            return;
        }
    #endif

    // If Start is pressed now, and was not pressed before...
    if (controllerState & PAD_START && !(lastControllerState & PAD_START)) {
        gameState = GAME_STATE_PAUSED;
        return;
    }
    if (playerControlsLockTime) {
        // If your controls are locked, just tick down the timer until they stop being locked. Don't read player input.
        playerControlsLockTime--;
    } else {
        if (controllerState & PAD_RIGHT && playerXVelocity >= (0 - PLAYER_VELOCITY_NUDGE)) {
            // If you're moving right, and you're not at max, speed up.
            if (playerXVelocity < maxVelocity) {
                playerXVelocity += PLAYER_VELOCITY_ACCEL;
            // If you're over max somehow, we'll slow you down a little.
            } else if (playerXVelocity > maxVelocity) {
                playerXVelocity -= PLAYER_VELOCITY_ACCEL;
            }
        } else if (controllerState & PAD_LEFT && playerXVelocity <= (0 + PLAYER_VELOCITY_NUDGE)) {
            // If you're moving left, and you're not at max, speed up.
            if (ABS(playerXVelocity) < maxVelocity) {
                playerXVelocity -= PLAYER_VELOCITY_ACCEL;
            // If you're over max, slow you down a little...
            } else if (ABS(playerXVelocity) > maxVelocity) { 
                playerXVelocity += PLAYER_VELOCITY_ACCEL;
            }
        } else if (playerXVelocity != 0) {
            // Not pressing anything? Let's slow you back down...
            if (playerXVelocity > 0) {
                playerXVelocity -= PLAYER_VELOCITY_ACCEL;
            } else {
                playerXVelocity += PLAYER_VELOCITY_ACCEL;
            }
        }

        if (controllerState & PAD_UP && playerYVelocity <= (0 + PLAYER_VELOCITY_NUDGE)) {
            if (ABS(playerYVelocity) < maxVelocity) {
                playerYVelocity -= PLAYER_VELOCITY_ACCEL;
            } else if (ABS(playerYVelocity) > maxVelocity) {
                playerYVelocity += PLAYER_VELOCITY_ACCEL;
            }
        } else if (controllerState & PAD_DOWN && playerYVelocity >= (0 - PLAYER_VELOCITY_NUDGE)) {
            if (playerYVelocity < maxVelocity) {
                playerYVelocity += PLAYER_VELOCITY_ACCEL;
            } else if (playerYVelocity > maxVelocity) {
                playerYVelocity -= PLAYER_VELOCITY_ACCEL;
            }
        } else { 
            if (playerYVelocity > 0) {
                playerYVelocity -= PLAYER_VELOCITY_ACCEL;
            } else if (playerYVelocity < 0) {
                playerYVelocity += PLAYER_VELOCITY_ACCEL;
            }
        }

        // Now, slowly adjust to the grid as possible, if the player isn't pressing a direction. 
        #if PLAYER_MOVEMENT_STYLE == MOVEMENT_STYLE_GRID
            if (playerYVelocity != 0 && playerXVelocity == 0 && (controllerState & (PAD_LEFT | PAD_RIGHT)) == 0) {
                if ((char)((playerXPosition + PLAYER_POSITION_TILE_MASK_MIDDLE) & PLAYER_POSITION_TILE_MASK) > (char)(PLAYER_POSITION_TILE_MASK_MIDDLE)) {
                    playerXVelocity = 0-PLAYER_VELOCITY_NUDGE;
                } else if ((char)((playerXPosition + PLAYER_POSITION_TILE_MASK_MIDDLE) & PLAYER_POSITION_TILE_MASK) < (char)(PLAYER_POSITION_TILE_MASK_MIDDLE)) {
                    playerXVelocity = PLAYER_VELOCITY_NUDGE;
                } // If equal, do nothing. That's our goal.
            }

            if (playerXVelocity != 0 && playerYVelocity == 0 && (controllerState & (PAD_UP | PAD_DOWN)) == 0) {
                if ((char)((playerYPosition + PLAYER_POSITION_TILE_MASK_MIDDLE + PLAYER_POSITION_TILE_MASK_EXTRA_NUDGE) & PLAYER_POSITION_TILE_MASK) > (char)(PLAYER_POSITION_TILE_MASK_MIDDLE)) {
                    playerYVelocity = 0-PLAYER_VELOCITY_NUDGE;
                } else if ((char)((playerYPosition + PLAYER_POSITION_TILE_MASK_MIDDLE + PLAYER_POSITION_TILE_MASK_EXTRA_NUDGE) & PLAYER_POSITION_TILE_MASK) < (char)(PLAYER_POSITION_TILE_MASK_MIDDLE)) {
                    playerYVelocity = PLAYER_VELOCITY_NUDGE;
                } // If equal, do nothing. That's our goal.
            }
        #endif
    }

    // While we're at it, tick down the invulnerability timer if needed
    if (playerInvulnerabilityTime) {
        playerInvulnerabilityTime--;
    }

    // This will knock out the player's speed if they hit anything.
    test_player_tile_collision();
    handle_player_sprite_collision();

    rawXPosition = (playerXPosition >> PLAYER_POSITION_SHIFT);
    rawYPosition = (playerYPosition >> PLAYER_POSITION_SHIFT);
    if (rawXPosition > (SCREEN_EDGE_RIGHT+4) && rawXPosition < SCREEN_EDGE_LEFT) { // Left screen has to wrap, so this is the opposite of what you'd expect.
        if (playerInvulnerabilityTime) {
            playerXPosition -= playerXVelocity;
            rawXPosition = (playerXPosition >> PLAYER_POSITION_SHIFT);
        } else {
            playerDirection = SPRITE_DIRECTION_LEFT;
            gameState = GAME_STATE_SCREEN_SCROLL;
            playerOverworldPosition--;
        }
    } else if (rawXPosition > SCREEN_EDGE_RIGHT && rawXPosition < (SCREEN_EDGE_RIGHT+4)) {
        // We use sprite direction to determine which direction to scroll in, so be sure this is set properly.
        if (playerInvulnerabilityTime) {
            playerXPosition -= playerXVelocity;
            rawXPosition = (playerXPosition >> PLAYER_POSITION_SHIFT);
        } else {
            playerDirection = SPRITE_DIRECTION_RIGHT;
            gameState = GAME_STATE_SCREEN_SCROLL;
            playerOverworldPosition++;
        }
    } else if (rawYPosition > SCREEN_EDGE_BOTTOM) {
        if (playerInvulnerabilityTime) {
            playerYPosition -= playerYVelocity;
            rawYPosition = (playerYPosition >> PLAYER_POSITION_SHIFT);
        } else {
            playerDirection = SPRITE_DIRECTION_DOWN;
            gameState = GAME_STATE_SCREEN_SCROLL;
            playerOverworldPosition += 8;
        }
    } else if (rawYPosition < SCREEN_EDGE_TOP) {
        if (playerInvulnerabilityTime) {
            playerYPosition -= playerYVelocity;
            rawYPosition = (playerYPosition >> PLAYER_POSITION_SHIFT);
        } else {
            playerDirection = SPRITE_DIRECTION_UP;
            gameState = GAME_STATE_SCREEN_SCROLL;
            playerOverworldPosition -= 8;
        }
    }

}

void test_player_tile_collision(void) {

	if (playerYVelocity != 0) {
        collisionTempYInt = playerYPosition + PLAYER_Y_OFFSET_EXTENDED + playerYVelocity;
        collisionTempXInt = playerXPosition + PLAYER_X_OFFSET_EXTENDED;

		collisionTempY = ((collisionTempYInt) >> PLAYER_POSITION_SHIFT) - HUD_PIXEL_HEIGHT;
		collisionTempX = ((collisionTempXInt) >> PLAYER_POSITION_SHIFT);

        collisionTempYInt += PLAYER_HEIGHT_EXTENDED;
        collisionTempXInt += PLAYER_WIDTH_EXTENDED;

        collisionTempYBottom = ((collisionTempYInt) >> PLAYER_POSITION_SHIFT) - HUD_PIXEL_HEIGHT;
        collisionTempXRight = (collisionTempXInt) >> PLAYER_POSITION_SHIFT;
        
        // Due to how we are calculating the sprite's position, there is a slight chance we can exceed the area that our
        // map takes up near screen edges. To compensate for this, we clamp the Y position of our character to the 
        // window between 0 and 192 pixels, which we can safely test collision within.

        // If collisionTempY is > 240, it can be assumed we calculated a position less than zero, and rolled over to 255
        if (collisionTempY > 240) {
            collisionTempY = 0;
        }
        // The lowest spot we can test collision is at pixel 192 (the 12th 16x16 tile). If we're past that, bump ourselves back.
        if (collisionTempYBottom > 190) { 
            collisionTempYBottom = 190;
        }

		if (playerYVelocity < 0) {
            // We're going up - test the top left, and top right
			if (test_collision(currentMap[PLAYER_MAP_POSITION(collisionTempX, collisionTempY)], 1) || test_collision(currentMap[PLAYER_MAP_POSITION(collisionTempXRight, collisionTempY)], 1)) {
                playerYVelocity = 0;
                playerControlsLockTime = 0;
            }
            if (!playerControlsLockTime && playerYVelocity < (0-PLAYER_VELOCITY_NUDGE)) {
                playerDirection = SPRITE_DIRECTION_UP;
            }
		} else {
            // Okay, we're going down - test the bottom left and bottom right
			if (test_collision(currentMap[PLAYER_MAP_POSITION(collisionTempX, collisionTempYBottom)], 1) || test_collision(currentMap[PLAYER_MAP_POSITION(collisionTempXRight, collisionTempYBottom)], 1)) {
                playerYVelocity = 0;
                playerControlsLockTime = 0;

			}
            if (!playerControlsLockTime && playerYVelocity > PLAYER_VELOCITY_NUDGE) {
                playerDirection = SPRITE_DIRECTION_DOWN;
            }
		}
    }

	if (playerXVelocity != 0) {
        collisionTempXInt = playerXPosition + PLAYER_X_OFFSET_EXTENDED + playerXVelocity;
        collisionTempYInt = playerYPosition + PLAYER_Y_OFFSET_EXTENDED + playerYVelocity;
		collisionTempX = (collisionTempXInt) >> PLAYER_POSITION_SHIFT;
		collisionTempY = ((collisionTempYInt) >> PLAYER_POSITION_SHIFT) - HUD_PIXEL_HEIGHT;

        collisionTempXInt += PLAYER_WIDTH_EXTENDED;
        collisionTempYInt += PLAYER_HEIGHT_EXTENDED;

        collisionTempYBottom = ((collisionTempYInt) >> PLAYER_POSITION_SHIFT) - HUD_PIXEL_HEIGHT;
        collisionTempXRight = ((collisionTempXInt) >> PLAYER_POSITION_SHIFT);
        // The lowest spot we can test collision is at pixel 192 (the 12th 16x16 tile). If we're past that, bump ourselves back.
        if (collisionTempYBottom > 190) {
            collisionTempYBottom = 190;
        }



        // Depending on how far to the left/right the player is, there's a chance part of our bounding box falls into
        // the next column of tiles on the opposite side of the screen. This if statement prevents those collisions.
        if (collisionTempX > 2 && collisionTempX < 238) {
            if (playerXVelocity < 0) {
                // Okay, we're moving left. Need to test the top-left and bottom-left
                if (test_collision(currentMap[PLAYER_MAP_POSITION(collisionTempX, collisionTempY)], 1) || test_collision(currentMap[PLAYER_MAP_POSITION(collisionTempX, collisionTempYBottom)], 1)) {
                    playerXVelocity = 0;
                    playerControlsLockTime = 0;

                }
                if (!playerControlsLockTime && playerXVelocity < (0-PLAYER_VELOCITY_NUDGE)) {
                    playerDirection = SPRITE_DIRECTION_LEFT;
                }
            } else {
                // Going right - need to test top-right and bottom-right
                if (test_collision(currentMap[PLAYER_MAP_POSITION(collisionTempXRight, collisionTempY)], 1) || test_collision(currentMap[PLAYER_MAP_POSITION(collisionTempXRight, collisionTempYBottom)], 1)) {
                    playerXVelocity = 0;
                    playerControlsLockTime = 0;

                }
                if (!playerControlsLockTime && playerXVelocity > PLAYER_VELOCITY_NUDGE) {
                    playerDirection = SPRITE_DIRECTION_RIGHT;
                }
            }
        }
	}

    playerXPosition += playerXVelocity;
    playerYPosition += playerYVelocity;

}

#define currentMapSpriteIndex tempChar1
void handle_player_sprite_collision(void) {

    if (teleportCooldownTime != 0) {
        --teleportCooldownTime;
    }

    // We store the last sprite hit when we update the sprites in `map_sprites.c`, so here all we have to do is react to it.
    if (lastPlayerSpriteCollisionId != NO_SPRITE_HIT) {
        currentMapSpriteIndex = lastPlayerSpriteCollisionId<<MAP_SPRITE_DATA_SHIFT;
        switch (currentMapSpriteData[(currentMapSpriteIndex) + MAP_SPRITE_DATA_POS_TYPE]) {
            case SPRITE_TYPE_HEALTH:
                // This if statement ensures that we don't remove hearts if you don't need them yet.
                if (playerHealth < playerMaxHealth) {
                    playerHealth += currentMapSpriteData[currentMapSpriteIndex + MAP_SPRITE_DATA_POS_HEALTH];
                    if (playerHealth > playerMaxHealth) {
                        playerHealth = playerMaxHealth;
                    }
                    // Hide the sprite now that it has been taken.
                    currentMapSpriteData[(currentMapSpriteIndex) + MAP_SPRITE_DATA_POS_TYPE] = SPRITE_TYPE_OFFSCREEN;

                    // Play the heart sound!
                    sfx_play(SFX_HEART, SFX_CHANNEL_3);

                    // Mark the sprite as collected, so we can't get it again.
                    spritePersistanceLocation = ((currentWorldId - FIRST_MAP_BANK_ID) << 6) + playerOverworldPosition;
                    currentMapSpritePersistance[spritePersistanceLocation] |= bitToByte[lastPlayerSpriteCollisionId];
                }
                break;
            case SPRITE_TYPE_KEY:
                if (playerKeyCount < MAX_KEY_COUNT) {
                    playerKeyCount++;
                    currentMapSpriteData[(currentMapSpriteIndex) + MAP_SPRITE_DATA_POS_TYPE] = SPRITE_TYPE_OFFSCREEN;

                    sfx_play(SFX_KEY, SFX_CHANNEL_3);

                    // Mark the sprite as collected, so we can't get it again.
                    spritePersistanceLocation = ((currentWorldId - FIRST_MAP_BANK_ID) << 6) + playerOverworldPosition;
                    currentMapSpritePersistance[spritePersistanceLocation] |= bitToByte[lastPlayerSpriteCollisionId];
                }
                break;
            case SPRITE_TYPE_MONEY:
                playerMoneyCount += currentMapSpriteData[currentMapSpriteIndex + MAP_SPRITE_DATA_POS_MONEY];
                if ((playerMoneyCount & 0x0f) > 0x09) {
                    playerMoneyCount += 0x06;
                }
                if ((playerMoneyCount & 0xf0) > 0x90) {
                    playerMoneyCount += 0x60;
                }
                if (playerMoneyCount > 0x999) {
                    playerMoneyCount = 0x999;
                }
                sfx_play(SFX_MONEY, SFX_CHANNEL_3);

                currentMapSpriteData[(currentMapSpriteIndex) + MAP_SPRITE_DATA_POS_TYPE] = SPRITE_TYPE_OFFSCREEN;
                // Mark the sprite as collected, so we can't get it again.
                spritePersistanceLocation = ((currentWorldId - FIRST_MAP_BANK_ID) << 6) + playerOverworldPosition;
                currentMapSpritePersistance[spritePersistanceLocation] |= bitToByte[lastPlayerSpriteCollisionId];


                break;
            case SPRITE_TYPE_REGULAR_ENEMY:
            case SPRITE_TYPE_INVULNERABLE_ENEMY:

                if (playerInvulnerabilityTime) {
                    return;
                }
                playerHealth -= currentMapSpriteData[currentMapSpriteIndex + MAP_SPRITE_DATA_POS_DAMAGE]; 
                // Since playerHealth is unsigned, we need to check for wraparound damage. 
                // NOTE: If something manages to do more than 16 damage at once, this might fail.
                if (playerHealth == 0 || playerHealth > 240) {
                    gameState = GAME_STATE_GAME_OVER;
                    music_stop();
                    sfx_play(SFX_GAMEOVER, SFX_CHANNEL_1);
                    return;
                }
                // Knock the player back
                playerControlsLockTime = PLAYER_DAMAGE_CONTROL_LOCK_TIME;
                playerInvulnerabilityTime = PLAYER_DAMAGE_INVULNERABILITY_TIME;
                if (playerDirection == SPRITE_DIRECTION_LEFT) {
                    // Punt them back in the opposite direction
                    playerXVelocity = PLAYER_MAX_VELOCITY;
                    // Reverse their velocity in the other direction, too.
                    playerYVelocity = 0 - playerYVelocity;
                } else if (playerDirection == SPRITE_DIRECTION_RIGHT) {
                    playerXVelocity = 0-PLAYER_MAX_VELOCITY;
                    playerYVelocity = 0 - playerYVelocity;
                } else if (playerDirection == SPRITE_DIRECTION_DOWN) {
                    playerYVelocity = 0-PLAYER_MAX_VELOCITY;
                    playerXVelocity = 0 - playerXVelocity;
                } else { // Make being thrown downward into a catch-all, in case your direction isn't set or something.
                    playerYVelocity = PLAYER_MAX_VELOCITY;
                    playerXVelocity = 0 - playerXVelocity;
                }
                sfx_play(SFX_HURT, SFX_CHANNEL_2);

                
                break;
            case SPRITE_TYPE_DOOR: 
                // Doors without locks are very simple - they just open! Hide the sprite until the user comes back...
                // note that we intentionally *don't* store this state, so it comes back next time.
                currentMapSpriteData[(currentMapSpriteIndex) + MAP_SPRITE_DATA_POS_TILE_ID] = SPRITE_TILE_ID_OFFSCREEN;
                currentMapSpriteData[(currentMapSpriteIndex) + MAP_SPRITE_DATA_POS_SIZE_PALETTE] = SPRITE_SIZE_8PX_8PX;


                // If there's a value in the first byte of this, it probably has a teleport attached. Prepare to launch the player somewhere
                if (currentMap[MAP_DATA_EXTRA_START + ((lastPlayerSpriteCollisionId<<2))] != 0) {


                    // If we set a cooldown time, don't allow warping. This allows us to re-spawn the user into the doorway
                    // in the other map without them immediately trying to teleport again.
                    if (teleportCooldownTime != 0) {
                        break;
                    }

                    // Next, figure out if the player is completely within the tile, and if so, teleport them.
                    // Note that this isn't a normal collision test, so don't try to reuse it as one ;)
                    
                    // Calculate position of this sprite...
                    tempSpriteCollisionX = ((currentMapSpriteData[currentMapSpriteIndex + MAP_SPRITE_DATA_POS_X]) + ((currentMapSpriteData[currentMapSpriteIndex + MAP_SPRITE_DATA_POS_X + 1]) << 8));
                    tempSpriteCollisionY = ((currentMapSpriteData[currentMapSpriteIndex + MAP_SPRITE_DATA_POS_Y]) + ((currentMapSpriteData[currentMapSpriteIndex + MAP_SPRITE_DATA_POS_Y + 1]) << 8));

                    // Test to see if the sprite is completely contained within the boundaries of this sprite. 
                    // To make it a little loose, we make the sprite look like it is 2 pixels wider on all sides. 
                    // (subtract 2 from x and y positions for comparison, then add 2 to the width and height.)
                    if (
                        playerXPosition > tempSpriteCollisionX - (2 << PLAYER_POSITION_SHIFT) &&
                        playerXPosition + PLAYER_WIDTH_EXTENDED < tempSpriteCollisionX + (18 << PLAYER_POSITION_SHIFT) &&
                        playerYPosition > tempSpriteCollisionY - (2 << PLAYER_POSITION_SHIFT) &&
                        playerYPosition + PLAYER_HEIGHT_EXTENDED < tempSpriteCollisionY + (18 << PLAYER_POSITION_SHIFT)
                    ) {
                        // Okay, we are going to warp the user!

                        // Swap worlds
                        currentWorldId = currentMap[MAP_DATA_EXTRA_START + ((lastPlayerSpriteCollisionId<<2))];
                        
                        // Look up what world map tile to place the player on, from our big table, and switch to that tile.
                        playerOverworldPosition = currentMap[MAP_DATA_EXTRA_START + ((lastPlayerSpriteCollisionId<<2)) + 1];
                        
                        // Set our cooldown timer, so when the user shows up on the new map inside the door, they are not teleported.
                        teleportCooldownTime = TELEPORT_COOLDOWN_TIME;
                        // Switch to a new gameState that tells our game to animate your transition into this new world.
                        gameState = GAME_STATE_TELEPORTING;
                        // Will use this later to figure out the player's x/y coords
                        teleportDestinationPointer = lastPlayerSpriteCollisionId<<2;

                    }
                }



                break;
            case SPRITE_TYPE_LOCKED_DOOR:
                // First off, do you have a key? If so, let's just make this go away...
                if (playerKeyCount > 0) {
                    playerKeyCount--;
                    currentMapSpriteData[(currentMapSpriteIndex) + MAP_SPRITE_DATA_POS_TILE_ID] = SPRITE_TILE_ID_OFFSCREEN;
                    // Switch it to a reuglar door, to take advantage of the teleport logic in that area.
                    currentMapSpriteData[(currentMapSpriteIndex) + MAP_SPRITE_DATA_POS_TYPE] = SPRITE_TYPE_DOOR;
                    currentMapSpriteData[(currentMapSpriteIndex) + MAP_SPRITE_DATA_POS_SIZE_PALETTE] = SPRITE_SIZE_8PX_8PX;


                    // Mark the door as gone, so it doesn't come back.
                    spritePersistanceLocation = ((currentWorldId - FIRST_MAP_BANK_ID) << 6) + playerOverworldPosition;
                    currentMapSpritePersistance[spritePersistanceLocation] |= bitToByte[lastPlayerSpriteCollisionId];
                    break;
                }
                // So you don't have a key...
                // Okay, we collided with a door before we calculated the player's movement. After being moved, does the 
                // new player position also collide? If so, stop it. Else, let it go.

                // Calculate position...
                tempSpriteCollisionX = ((currentMapSpriteData[currentMapSpriteIndex + MAP_SPRITE_DATA_POS_X]) + ((currentMapSpriteData[currentMapSpriteIndex + MAP_SPRITE_DATA_POS_X + 1]) << 8));
                tempSpriteCollisionY = ((currentMapSpriteData[currentMapSpriteIndex + MAP_SPRITE_DATA_POS_Y]) + ((currentMapSpriteData[currentMapSpriteIndex + MAP_SPRITE_DATA_POS_Y + 1]) << 8));

                // Are we colliding?
                // NOTE: We take a bit of a shortcut here and assume all doors are 16x16 (the hard-coded 16 value below)
                if (
                    playerXPosition < tempSpriteCollisionX + (16 << PLAYER_POSITION_SHIFT) &&
                    playerXPosition + PLAYER_WIDTH_EXTENDED > tempSpriteCollisionX &&
                    playerYPosition < tempSpriteCollisionY + (16 << PLAYER_POSITION_SHIFT) &&
                    playerYPosition + PLAYER_HEIGHT_EXTENDED > tempSpriteCollisionY
                ) {
                    playerXPosition -= playerXVelocity;
                    playerYPosition -= playerYVelocity;
                    playerControlsLockTime = 0;
                }
                break;
            case SPRITE_TYPE_ENDGAME:
                gameState = GAME_STATE_CREDITS;
                break;
            case SPRITE_TYPE_NPC:

                // No collision testing, just let the user talk.
                if (controllerState & PAD_A && !(lastControllerState & PAD_A)) {
                    // Show the text for the player on the first screen
                    lookup_ptr_data();
                    trigger_game_text_banked(ptrTextData, textBankNum);
                }
                break;


        }

    }
}
