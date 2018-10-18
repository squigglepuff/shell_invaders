/*
 * Space Invaders in yer shell!
 * (c) 2016 Travis M Ervin
 * 4 Spaces per TAB ; 120 Columns
 *
 * Desc:
 *    This here is the Space Invaders in a BASH terminal. You can play it till you dead.
 *    Ya get 3 lives, and go till ya drop.
 *
 *    Characters:
 *        <^>             Player
 *     [###9###]          Barrier ('9' represents the remaining strength)
 *         @              Class 1 Enemy
 *         $              Class 2 Enemy
 *         &              Class 3 Enemy
 *        <~>             UFO
 *         .              Bullet
 *         ?              Random Drop
 *
 *    Keys:
 *        A               Move Left
 *        D               Move Right
 *        W               Shoot bullet
 *        ESC             Quit Game
 *
 * Game Layout:
 *     Every game object, has a center, this center is where the position of the entity is drawn from. The game board coordinates are identical to those in use by the terminal (top-left == origin).
 *     Player is on the line that is exactly 80% of the height.
 */
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <ctime>
#include <cmath>
#include <vector>
#include <list>

// Linux specific headers.
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <ncurses.h>

// Custom datatypes used by the game for generic type usage.
typedef unsigned char byte;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef float real; //!< Here we use the Mathematical term "real" instead of "number" to help prevent confusion.

// This data structure is used to for the various objects in the game.
struct GameObject
{
    real miXPos; //!< X-Position (column) of the object's center character.
    real miYPos; //!< Y-Position (row) of the object's center character.
    const char* msCharStr; //!< The actual string that represents the object.
    u32 miValue; //!< Point value assigned to this entity.

    GameObject() : miXPos(0), miYPos(0), msCharStr(nullptr), miValue(0) {}
};

// Errors that are thrown during runtime.
enum EError
{
    EError_OK, //!< No error occurred.
    EError_Unknown, //!< An unknown error occurred.
    EError_SegFault, //!< A segmentation fault (read-access violation) occurred.
    EError_MemCorrupt, //!< Memory corruption (write-access violation) occurred.
    EError_AssertPop, //!< A psuedo-assert popped (non-terminating assert).
    EError_InvalidArg //!< An invalid argument was passed.
};

// Function prototyping.
EError MoveHorde(); //!< This moves the horde of enemies.
EError DrawHorde(); //!< This draws the horde of enemies.
EError DrawPlayer(GameObject *pPlayer); //!< This draws the player, bullets, and barriers
EError DrawAll(GameObject *pPlayer, GameObject *pScore); //!< This draws all objects.
EError GetKeyPress(GameObject *pPlayer);
EError CreateBoard(GameObject *pPlyr);
bool CheckEnemyCollision(GameObject *pBullet, GameObject* apScore);
bool CheckBarrierCollision(GameObject *pBullet);
u32 GetScoreXPosition(u32 aiXTermWidth, const char* apStr);
void ResetTerminalMode();
void SetTerminalMode();
void DelayExec(u64 iMilliseconds);
EError SaveScore(u32 aiScore);
u32 GetScore();
EError DrawIntro();

// Global objects.
GameObject g_xTerm;
GameObject *g_pUFO;
struct termios g_sOrigTermios;
bool g_bUpdateChar = true;
bool g_bRunning = true;
bool g_bHordeMoveRight = false;
bool g_bUFOActive = false;
bool g_bHasColors = true;
bool g_bGameOver = false;
bool g_bWin = false;
bool g_bMoveDown = false;
bool g_bScoreSaved = false;
bool g_bIsIntro = true;
std::vector<GameObject*> g_vBullets;
std::vector<GameObject*> g_vBarriers;
std::vector<GameObject*> g_vHorde;
u32 g_iHordeMoveTimer = 0;
u32 g_iUFOMoveTimer = 0;
u32 g_iFireCooldown = 0;
u32 g_iHiScore = 0;
u32 g_iLives = 3;
real g_nHordeReset = 30;

int main(void)
{
    // Clear the xterm.
    fprintf(stdout, "\e[H\e[J");

    // Set the cursor to be invisible!
    fprintf(stdout, "\e[?25l");

    // Get the window size in rows and columns.
    struct winsize wSize;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &wSize);

    if (0 != wSize.ws_col && 0 != wSize.ws_row)
    {
        g_xTerm.miXPos = wSize.ws_col;
        g_xTerm.miYPos = wSize.ws_row;
    }
    else
    {
        fprintf(stderr, "Was unable to get xTerm size!\n");
        return -1;
    }

    // Set the terminal mode.
    SetTerminalMode();

    GameObject *pPlyr = new GameObject();
    pPlyr->miXPos = (g_xTerm.miXPos / 2) - 1;
    pPlyr->miYPos = g_xTerm.miYPos * 0.875;
    pPlyr->msCharStr = "<^>";

    GameObject *pScore = new GameObject();
    pScore->miXPos = GetScoreXPosition(g_xTerm.miXPos, "Score: %ld    Hi-Score: %ld    Lives: %d");
    pScore->miYPos = g_xTerm.miYPos - 1;
    pScore->msCharStr = "Score: %ld    Hi-Score: %ld    Lives: %d";

    // Lastly, allocate the UFO.
    g_pUFO = new GameObject();
    g_pUFO->miXPos = g_xTerm.miXPos - 2;
    g_pUFO->miYPos = 1;
    g_pUFO->miValue = 200;
    g_pUFO->msCharStr = "<~~~>";

    if (NULL == pPlyr)
    {
        // Print an error.
        fprintf(stderr, "Was unable to allocate player!\n");
        return -2;
    }

    // Now we need to initialize NCURSES.
    initscr();
    raw();
    noecho();
    nonl();
    keypad(stdscr, true);
    nodelay(stdscr, true);

    g_bHasColors = has_colors();
    g_bGameOver = false;
    g_iHiScore = GetScore();

    if (g_bHasColors)
    {
        start_color();
        init_pair(1, COLOR_RED, COLOR_BLACK);
        init_pair(2, COLOR_GREEN, COLOR_BLACK);
        init_pair(3, COLOR_YELLOW, COLOR_BLACK);
        init_pair(4, COLOR_WHITE, COLOR_BLACK);
    }

    // Run!
    while (g_bRunning)
    {
        // Before Drawing, pick a random number and if it's within a range, spawn the UFO.
        // The UFO has a >1% spawn chance, so we take a rand() % 100, and if the number is between 35-40, spawn the UFO. (30 and 40 picked arbitrarily).
        u32 iSpawnUFO = (rand() % 1000) + 1;

        if (542 > iSpawnUFO && 540 < iSpawnUFO)
        {
            // Spawn the UFO!
            g_bUFOActive = true;
        }

        // Draw!
        if (EError_OK != DrawAll(pPlyr, pScore))
        {
            fprintf(stderr, "An unknown error occurred! ABORTING!\n");
            break;
        }

        DelayExec((1000/60));
    }
    endwin();

    // Clean up.
    delete pScore;
    delete g_pUFO;
    delete pPlyr;

    // Reset the cursor and xterm.
    fprintf(stdout, "\e[34h\e[?25h\e[0m");

    return 0;
}

EError MoveHorde()
{
    if (0 == g_iHordeMoveTimer)
    {
        if (!g_bMoveDown)
        {
            for (int iIdx = 0; iIdx < g_vHorde.size(); ++iIdx)
            {
                if (g_vHorde.at(iIdx)->miXPos >= (g_xTerm.miXPos - 1))
                {
                    // Move down instead.
                    g_bMoveDown = true;
                    g_bHordeMoveRight = false;
                }
                else if (0 >= g_vHorde.at(iIdx)->miXPos)
                {
                    g_bMoveDown = true;
                    g_bHordeMoveRight = true;
                }

                // Are we gonna fire a bullet?
                u32 iFire = (rand() % 1000) + 1;

                if (iFire == 543)
                {
                    // We need to alloc. a new bullet and place it in the bullet vector so it can be drawn.
                    GameObject *pNewBull = new GameObject();
                    pNewBull->miXPos = g_vHorde.at(iIdx)->miXPos;
                    pNewBull->miYPos = g_vHorde.at(iIdx)->miYPos + 1;
                    pNewBull->msCharStr = ".";
                    pNewBull->miValue = 1;

                    g_vBullets.push_back(pNewBull);
                }
            }
        }
        else
        {
            g_bMoveDown = false;
        }

        if (g_bHordeMoveRight)
        {
            // Move the horde right.
            for (int iIdx = 0; iIdx < g_vHorde.size(); ++iIdx)
            {
                // Clear the position.
                mvaddch(g_vHorde.at(iIdx)->miYPos, g_vHorde.at(iIdx)->miXPos, ' ');

                if (g_bMoveDown)
                {
                    ++g_vHorde.at(iIdx)->miYPos;
                }
                else if (!g_bMoveDown)
                {
                    ++g_vHorde.at(iIdx)->miXPos;
                }

                // Check for game over.
                if (g_vBarriers.at(g_vBarriers.size()-1)->miYPos <= g_vHorde.at(iIdx)->miYPos)
                {
                    g_bGameOver = true;
                }
            }
        }
        else
        {
            // Move the horde left.
            for (int iIdx = 0; iIdx < g_vHorde.size(); ++iIdx)
            {
                // Clear the position.
                mvaddch(g_vHorde.at(iIdx)->miYPos, g_vHorde.at(iIdx)->miXPos, ' ');

                if (g_bMoveDown)
                {
                    ++g_vHorde.at(iIdx)->miYPos;
                }
                else if (!g_bMoveDown)
                {
                    --g_vHorde.at(iIdx)->miXPos;
                }

                // Check for game over.
                if (g_vBarriers.at(g_vBarriers.size()-1)->miYPos <= g_vHorde.at(iIdx)->miYPos)
                {
                    g_bGameOver = true;
                }
            }
        }

        // Reset the timer.
        g_iHordeMoveTimer = g_nHordeReset;
    }
    else
    {
        --g_iHordeMoveTimer;
    }

    return EError_OK;
}

EError DrawHorde()
{
    // Draw the horde.
    for (int iIdx = (g_vHorde.size() - 1); iIdx >= 0; --iIdx)
    {
        if (nullptr != g_vHorde.at(iIdx))
        {
            mvprintw(g_vHorde.at(iIdx)->miYPos, g_vHorde.at(iIdx)->miXPos, g_vHorde.at(iIdx)->msCharStr);
        }
        else
        {
            return EError_AssertPop;
        }
    }
    return EError_OK;
}

EError DrawPlayer(GameObject *pPlayer)
{
    if (NULL != pPlayer)
    {
        move((pPlayer->miYPos), (pPlayer->miXPos - 1));

        if (g_bHasColors)
        {
            attron(COLOR_PAIR(2));
            printw(pPlayer->msCharStr);
            attroff(COLOR_PAIR(2));
        }
        else
        {
            printw(pPlayer->msCharStr);
        }
        return EError_OK;
    }
    else
    {
        return EError_AssertPop;
    }

    return EError_Unknown;
}

EError DrawAll(GameObject *pPlayer, GameObject *pScore)
{
    // FIRST, CHECK FOR INTRO!
    if (g_bIsIntro)
    {
        DrawIntro();
        GetKeyPress(pPlayer);
        return EError_OK;
    }

    // Next, Check for game over.
    if (g_bGameOver)
    {
        const char *pStr = "Game Over!";
        u32 iMidX = g_xTerm.miXPos / 2;
        u32 iMidY = g_xTerm.miYPos / 2;
        u32 iMidStr = strlen(pStr) / 2;

        clear();

        if (g_bHasColors)
        {
            attron(COLOR_PAIR(3));
            mvprintw(iMidY, (iMidX - iMidStr), pStr);
            attroff(COLOR_PAIR(3));
        }
        else
        {
            mvprintw(iMidY, (iMidX - iMidStr), pStr);
        }

        SaveScore(pScore->miValue);
    }

    if (g_bWin)
    {
        const char *pStr = "You Win!";
        u32 iMidX = g_xTerm.miXPos / 2;
        u32 iMidY = g_xTerm.miYPos / 2;
        u32 iMidStr = strlen(pStr) / 2;

        clear();

        if (g_bHasColors)
        {
            attron(COLOR_PAIR(2));
            mvprintw(iMidY, (iMidX - iMidStr), pStr);
            attroff(COLOR_PAIR(2));
        }
        else
        {
            mvprintw(iMidY, (iMidX - iMidStr), pStr);
        }

        SaveScore(pScore->miValue);
    }

    // Draw the UFO.
    if (g_bUFOActive)
    {
        if (0 == g_iUFOMoveTimer)
        {
            if (0 >= g_pUFO->miXPos)
            {
                g_bUFOActive = false;
                g_pUFO->miXPos = g_xTerm.miXPos + 2;
            }
            else
            {
                // Move the UFO and draw it.
                // Clear the old position first.
                mvprintw(g_pUFO->miYPos, g_pUFO->miXPos - 2, "     ");

                --g_pUFO->miXPos;

                if (g_bHasColors)
                {
                    attron(COLOR_PAIR(1));
                    mvprintw(g_pUFO->miYPos, g_pUFO->miXPos - 2, g_pUFO->msCharStr);
                    attroff(COLOR_PAIR(1));
                }
                else
                {
                    mvprintw(g_pUFO->miYPos, g_pUFO->miXPos - 2, g_pUFO->msCharStr);
                }
            }

            g_iUFOMoveTimer = 2;
        }
        else
        {
            --g_iUFOMoveTimer;
        }
    }

    // Iterate through and draw + move the bullets.
    for (int iIdx = (g_vBullets.size() - 1); iIdx >= 0; --iIdx)
    {
        // Clear the bullet's old position first.
        mvaddch(g_vBullets.at(iIdx)->miYPos, g_vBullets.at(iIdx)->miXPos, ' ');

        // Check the direction of the bullet and move accordingly.
        if (0 == g_vBullets.at(iIdx)->miValue)
        {
            // Player bullet, move up.
            g_vBullets.at(iIdx)->miYPos -= 1;
        }
        else
        {
            // Enemy bullet, move down.
            g_vBullets.at(iIdx)->miYPos += 0.2;
        }

        // Check to make sure the bullet is still on the board.
        if (0 >= floor(g_vBullets.at(iIdx)->miYPos) || g_xTerm.miYPos <= floor(g_vBullets.at(iIdx)->miYPos))
        {
            // Pop the bullet off the vector.
            delete g_vBullets.at(iIdx);
            g_vBullets.erase(g_vBullets.begin()+iIdx);
        }
        else
        {
            // Draw the bullet.
            if (g_bHasColors)
            {
                attron(COLOR_PAIR(3));
                mvprintw(g_vBullets.at(iIdx)->miYPos, g_vBullets.at(iIdx)->miXPos, g_vBullets.at(iIdx)->msCharStr);
                attroff(COLOR_PAIR(3));
            }
            else
            {
                mvprintw(g_vBullets.at(iIdx)->miYPos, g_vBullets.at(iIdx)->miXPos, g_vBullets.at(iIdx)->msCharStr);
            }

            // Check for Bullet collisions.
            if (CheckBarrierCollision(g_vBullets.at(iIdx)))
            {
                // Pop the bullet off the vector.
                mvaddch(g_vBullets.at(iIdx)->miYPos, g_vBullets.at(iIdx)->miXPos, ' ');
                delete g_vBullets.at(iIdx);
                g_vBullets.erase(g_vBullets.begin()+iIdx);
            }
            else
            {
                if (0 == g_vBullets.at(iIdx)->miValue)
                {
                    // Check for enemy collision.
                    if (CheckEnemyCollision(g_vBullets.at(iIdx), pScore))
                    {
                        // Pop the bullet off the vector.
                        mvaddch(g_vBullets.at(iIdx)->miYPos, g_vBullets.at(iIdx)->miXPos, ' ');
                        delete g_vBullets.at(iIdx);
                        g_vBullets.erase(g_vBullets.begin()+iIdx);
                    }
                }
                else if (0 != g_vBullets.at(iIdx)->miValue)
                {
                    // Check for player collision.
                    u32 iPlayerXMax = floor(pPlayer->miXPos + 1);
                    u32 iPlayerXMin = floor(pPlayer->miXPos - 1);
                    if ((iPlayerXMin <= floor(g_vBullets.at(iIdx)->miXPos) && floor(iPlayerXMax >= g_vBullets.at(iIdx)->miXPos)) && floor(pPlayer->miYPos) == floor(g_vBullets.at(iIdx)->miYPos))
                    {
                        // Pop the bullet off the vector.
                        mvaddch(g_vBullets.at(iIdx)->miYPos, g_vBullets.at(iIdx)->miXPos, ' ');
                        delete g_vBullets.at(iIdx);
                        g_vBullets.erase(g_vBullets.begin()+iIdx);

                        // Kill the player!
                        --g_iLives;
                        pPlayer->miXPos = (g_xTerm.miXPos / 2) - 1;
                        pPlayer->miYPos = g_xTerm.miYPos * 0.875;
                        clrtoeol();
                    }
                }
            }
        }
    }

    // Draw the barriers.
    for (int iIdx = (g_vBarriers.size() - 1); iIdx >= 0; --iIdx)
    {
        // Grab the barrier offset (x / 2).
        u32 miOffset = (strlen(g_vBarriers.at(iIdx)->msCharStr) - 1) / 2;

        // Print the barrier.
        if (g_bHasColors)
        {
            u16 iClr = 2;
            if (4 <= g_vBarriers.at(iIdx)->miValue && 7 > g_vBarriers.at(iIdx)->miValue)
            {
                iClr = 3;
            }
            else if (4 > g_vBarriers.at(iIdx)->miValue)
            {
                iClr = 1;
            }

            attron(COLOR_PAIR(iClr));
            mvprintw(g_vBarriers.at(iIdx)->miYPos, (g_vBarriers.at(iIdx)->miXPos - miOffset), g_vBarriers.at(iIdx)->msCharStr, g_vBarriers.at(iIdx)->miValue);
            attroff(COLOR_PAIR(iClr));
        }
        else
        {
            mvprintw(g_vBarriers.at(iIdx)->miYPos, (g_vBarriers.at(iIdx)->miXPos - miOffset), g_vBarriers.at(iIdx)->msCharStr, g_vBarriers.at(iIdx)->miValue);
        }
    }

    // Move and Draw the horde.
    if (g_bHasColors)
    {
        attron(COLOR_PAIR(4));
    }

    if (!g_bGameOver && !g_bWin)
    {
        MoveHorde();
        DrawHorde();
    }

    if (g_bHasColors)
    {
        attroff(COLOR_PAIR(4));
    }

    if (g_bUpdateChar)
    {
        move(pPlayer->miYPos, 0);
        clrtoeol();
        g_bUpdateChar = false;
    }

    if (!g_bGameOver && !g_bWin)
    {
        // Draw the character.
        DrawPlayer(pPlayer);

        if (g_bHasColors)
        {
            attron(COLOR_PAIR(4));
        }
    }

    // Lastly, draw the score.
    if (nullptr != pScore)
    {
        // Before the actual drawing happens, reposition the score to center it.
        pScore->miXPos = GetScoreXPosition(g_xTerm.miXPos, pScore->msCharStr);
        move(pScore->miYPos, pScore->miXPos);
        printw(pScore->msCharStr, pScore->miValue, g_iHiScore, g_iLives);
    }

    if (g_bHasColors)
    {
        attroff(COLOR_PAIR(4));
    }

    refresh();

    // Decrement the cooldown timer on the fire.
    g_iFireCooldown -= (0 >= g_iFireCooldown) ? 0 : 1;

    // Lastly, check for keypresses.
    GetKeyPress(pPlayer);

    return EError_OK;
}

EError GetKeyPress(GameObject *pPlayer)
{    
    int cChar = getch();
    if (cChar < 0)
    {
        return EError_Unknown;
    }
    else
    {
        // We have a character! Check and see if it's one we want, then discard.
        if (119 == cChar || 32 == cChar)
        {
            if (!g_bGameOver && !g_bWin)
            {
                if (0 == g_iFireCooldown)
                {
                    // We need to alloc. a new bullet and place it in the bullet vector so it can be drawn.
                    GameObject *pNewBull = new GameObject();
                    pNewBull->miXPos = pPlayer->miXPos;
                    pNewBull->miYPos = pPlayer->miYPos - 1;
                    pNewBull->msCharStr = "*";
                    pNewBull->miValue = 0;

                    g_vBullets.push_back(pNewBull);

                    g_iFireCooldown = 15;
                }
            }
        }
        else if (97 == cChar || KEY_LEFT == cChar)
        {
            if (!g_bGameOver && !g_bWin)
            {
                // Check to make sure we're not at the borders.
                if (0 < (pPlayer->miXPos - 1))
                {
                    // We're not, move the character left.
                    --pPlayer->miXPos;
                    g_bUpdateChar = true;
                }
            }
        }
        else if (100 == cChar || KEY_RIGHT == cChar)
        {
            if (!g_bGameOver && !g_bWin)
            {
                // Check to make sure we're not at the borders.
                if (g_xTerm.miXPos > (pPlayer->miXPos + 1))
                {
                    // We're not, move the character right.
                    ++pPlayer->miXPos;
                    g_bUpdateChar = true;
                }
            }
        }
        else if (27 == cChar)
        {
            if (g_bIsIntro)
            {
                // Exit out.
                g_bRunning = false;
            }
            else
            {
                g_bIsIntro = true;
                clear();
            }
        }
        else if (3 == cChar)
        {
            // Exit out.
            g_bRunning = false;
        }
        else if (13 == cChar) // ENTER
        {
            if (g_bIsIntro)
            {
                g_bIsIntro = false;
                clear();

                // Clear the horde and remake it.
                g_vHorde.clear();
                CreateBoard(pPlayer);
            }
        }
    }

    return EError_OK;
}

bool CheckBarrierCollision(GameObject *pBullet)
{
    if (nullptr != pBullet)
    {
        // Iterate through the barriers and check to see if one if hit.
        for (int iIdx = (g_vBarriers.size() - 1); iIdx >= 0; --iIdx)
        {
            if (floor(pBullet->miYPos) <= floor(g_vBarriers.at(iIdx)->miYPos))
            {
                // Check to see if the bullet character is in the same position as one of the characters for the barrier.
                u32 iStrLen = strlen(g_vBarriers.at(iIdx)->msCharStr) - 1;
                u32 iSize = iStrLen / 2;
                u32 iMax = (g_vBarriers.at(iIdx)->miXPos + iSize);
                u32 iMin = (g_vBarriers.at(iIdx)->miXPos - iSize);

                if (floor(pBullet->miXPos) <= floor(iMax) && floor(pBullet->miXPos) >= floor(iMin))
                {
                    // HIT!
                    --g_vBarriers.at(iIdx)->miValue;

                    // Check to see if the barrier is done for.
                    if (0 >= g_vBarriers.at(iIdx)->miValue)
                    {
                        // Grab the barrier offset (x / 2).
                        u32 miOffset = (strlen(g_vBarriers.at(iIdx)->msCharStr) - 1) / 2;

                        // Print the barrier.
                        mvprintw(g_vBarriers.at(iIdx)->miYPos, (g_vBarriers.at(iIdx)->miXPos - miOffset), "[!!!%d!!!]", 0);

                        delete g_vBarriers.at(iIdx);
                        g_vBarriers.erase(g_vBarriers.begin()+iIdx);
                    }

                    return true;
                }
            }
        }
    }

    return false;
}

bool CheckEnemyCollision(GameObject *pBullet, GameObject* apScore)
{
    if (nullptr != pBullet && !g_vHorde.empty())
    {
        // Iterate through the enemies and check for collision.
        for (int iIdx = (g_vHorde.size() - 1); iIdx >= 0; --iIdx)
        {
            if (floor(g_vHorde.at(iIdx)->miXPos) == floor(pBullet->miXPos) && floor(g_vHorde.at(iIdx)->miYPos) == floor(pBullet->miYPos))
            {
                // HIT!
                apScore->miValue += g_vHorde.at(iIdx)->miValue;

                // Cut the enemy from the vector and return true to remove the bullet.
                delete g_vHorde.at(iIdx);
                g_vHorde.erase(g_vHorde.begin()+iIdx);

                // Calculate the new horde timer reset amount.
                if (5 < g_nHordeReset)
                {
                    real nSpeedDiff = (30.0 - 5.0);
                    real nHordeSize = (g_vHorde.size() - 1.0);
                    g_nHordeReset -= nSpeedDiff / nHordeSize;
                }

                return true;
            }
        }
    }    

    // Check for UFO collision.
    if (g_bUFOActive)
    {
        if (floor(pBullet->miXPos) >= floor(g_pUFO->miXPos - 2) && floor(pBullet->miXPos) <= floor(g_pUFO->miXPos + 2) && floor(pBullet->miYPos) == floor(g_pUFO->miYPos))
        {
            apScore->miValue += g_pUFO->miValue;
            g_pUFO->miXPos = g_xTerm.miXPos - 2;
            g_pUFO->miYPos = 1;

            // Erase the UFO from the screen.
            move(1, 0);
            clrtoeol();
            g_bUFOActive = false;
            return true;
        }
    }

    // Check if we dun won.
    if (0 >= g_vHorde.size())
    {
        g_bWin = true;
    }

    return false;
}

void ResetTerminalMode()
{
    tcsetattr(0, TCSANOW, &g_sOrigTermios);
}

void SetTerminalMode()
{
    struct termios g_sNewTermios;

    /* take two copies - one for now, one for later */
    tcgetattr(0, &g_sOrigTermios);
    memcpy(&g_sNewTermios, &g_sOrigTermios, sizeof(g_sNewTermios));

    /* register cleanup handler, and set the new terminal mode */
    atexit(ResetTerminalMode);
    cfmakeraw(&g_sNewTermios);
    tcsetattr(0, TCSANOW, &g_sNewTermios);
}

u32 GetScoreXPosition(u32 aiXTermWidth, const char *apStr)
{
    u32 iRtnVal = 1; // We start at one that way if the function fails, the text isn't against the side of the term.
    u32 iStrLen = strlen(apStr);
    iRtnVal = (aiXTermWidth / 2) - (iStrLen / 2);
    return iRtnVal;
}

void DelayExec(u64 iMilliseconds)
{
    clock_t cStart = clock(); //!< Start the timer.
    double iPassed; //!< Number of clocks passed.
    bool bRun = true; //!< Should the loop run?
    while(bRun)
    {
        iPassed = (clock() - cStart) / (CLOCKS_PER_SEC / 1000);
        if(iPassed >= iMilliseconds)
        {
            bRun = false;
        }
    }
}

EError SaveScore(u32 aiScore)
{
    if (!g_bScoreSaved)
    {
        FILE *pFile = fopen("scores", "w+");

        if (!pFile)
        {
            return EError_Unknown;
        }

        char *pOut = new char[10];
        sprintf(pOut, "%d", aiScore);

        fwrite(pOut, sizeof(u32), 1, pFile);
        fclose(pFile);

        delete[] pOut;

        g_bScoreSaved = true;
    }
}

u32 GetScore()
{
    u32 iRtn = 0;

    FILE *pFile = fopen("scores", "r");

    if (pFile)
    {
        char *pOut = new char[10];
        fread(pOut, sizeof(u32), 1, pFile);
        fclose(pFile);

        iRtn = atoi(pOut);

        delete[] pOut;
    }

    return iRtn;
}

EError DrawIntro()
{
    // Determine the middle of the screen.
    u32 iXMid = g_xTerm.miXPos / 2;
    u32 iYMid = g_xTerm.miYPos / 2;
    u32 iStrMid = strlen("Welcome to Shell Invaders!") / 2;

    /*
     * Welcome to Shell Invaders!
     *
     * Controls:
     *      A   -   Move left
     *      D   -   Move right
     *      W   -   Shoot
     *      ESC -   Quit
     *
     * Press ENTER to begin!
     */
    mvprintw((iYMid - 4), (iXMid - iStrMid), "Welcome to Shell Invaders!");
    mvprintw((iYMid - 2), (iXMid - iStrMid), "Controls:");
    mvprintw((iYMid - 1), (iXMid - iStrMid), "\tA/Left\t-\tMove Left");
    mvprintw(iYMid, (iXMid - iStrMid), "\tD/Right\t-\tMove right");
    mvprintw((iYMid+1), (iXMid - iStrMid), "\tW/Space\t-\tShoot");
    mvprintw((iYMid+2), (iXMid - iStrMid), "\tESC\t-\tQuit/Return to Menu");
    mvprintw((iYMid+4), (iXMid - iStrMid), "Press ENTER to begin!");

    return EError_OK;
}

EError CreateBoard(GameObject *pPlyr)
{
    // First clear out the old crap.
    for (int iIdx = (g_vHorde.size() -1); iIdx >= 0; --iIdx)
    {
        delete g_vHorde.at(iIdx);
    }

    g_vHorde.clear();

    for (int iIdx = (g_vBarriers.size() - 1); iIdx >= 0; --iIdx)
    {
        delete g_vBarriers.at(iIdx);
    }

    g_vBarriers.clear();

    // Determine the amount of barriers to make.
    const char* csBarrierStr = "[###%d###]";
    u32 iNumBarriers = (g_xTerm.miXPos / (strlen(csBarrierStr) - 1)) / 2; //!< We subtract 1 from the string length because in printing, %d will equal a single digit number.
    u32 iBarrierXScale = g_xTerm.miXPos / iNumBarriers;
    u32 iLastX = iBarrierXScale / 2;

    for (u32 iIdx = 0; iIdx < iNumBarriers; ++iIdx)
    {
        // Allocate a new barrier object.
        GameObject *pObj = new GameObject();
        pObj->miXPos = iLastX;
        pObj->miYPos = pPlyr->miYPos - 2;
        pObj->msCharStr = csBarrierStr;
        pObj->miValue = 9; //!< This has special meaning here, it's the health of the barrier.

        // Push the barrier onto the vector.
        g_vBarriers.push_back(pObj);

        // Setup the next X position.
        iLastX += iBarrierXScale;
    }

    // Create the horde of enemies.
    u32 iBarrierY = pPlyr->miYPos - 2;
    u32 iAmntHoriz = (g_xTerm.miXPos - (iBarrierXScale * 2)) / 2; //!< Calculate the amount of horizontal enemies.
    u32 iAmntVert = (iBarrierY - 8); //!< Calculate the ammount of vertical lines in use. Subtract '8' as the lines start @ 3 and stop at 5 above barrier Y.
    u32 iEnemyX = iBarrierXScale; // Enemies start at X position of the first barrier.
    u32 iEnemyY = 3; // Vertical lines start @ 3.

    for (u32 iIdx = 0; iIdx < (iAmntHoriz * iAmntVert); ++iIdx)
    {
        // First check to make sure that we don't overflow the row.
        if (iEnemyX > (g_xTerm.miXPos - iBarrierXScale))
        {
            iEnemyX = iBarrierXScale;
            iEnemyY += 2;
        }

        // Make sure we don't overflow vertically.
        if (iEnemyY > (iBarrierY - 5))
        {
            break;
        }

        // Allocate a new object.
        GameObject *pObj = new GameObject();
        pObj->miXPos = iEnemyX;
        pObj->miYPos = iEnemyY;
        pObj->msCharStr = "";
        pObj->miValue = 0;

        // Determin the enemy stats (string and value).
        if (iEnemyY >= 3 && iEnemyY <= 5)
        {
            // Setup class 3.
            pObj->msCharStr = "&";
            pObj->miValue = 15;
        }
        else if (iEnemyY >= 7 && iEnemyY <= 9)
        {
            // Setup class 2.
            pObj->msCharStr = "$";
            pObj->miValue = 10;
        }
        else
        {
            // Setup class 1.
            pObj->msCharStr = "@";
            pObj->miValue = 5;
        }

        // Push the character onto the horde vector.
        g_vHorde.push_back(pObj);

        // Update the X-Position.
        iEnemyX += 2;
    }
}
