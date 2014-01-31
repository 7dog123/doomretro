/*
====================================================================

DOOM RETRO
A classic, refined DOOM source port. For Windows PC.

Copyright � 1993-1996 id Software LLC, a ZeniMax Media company.
Copyright � 2005-2014 Simon Howard.
Copyright � 2013-2014 Brad Harding.

This file is part of DOOM RETRO.

DOOM RETRO is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

DOOM RETRO is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with DOOM RETRO. If not, see http://www.gnu.org/licenses/.

====================================================================
*/

#include "z_zone.h"
#include "doomdef.h"
#include "p_local.h"

#include "s_sound.h"

// State.
#include "doomstat.h"
#include "r_state.h"
// Data.
#include "sounds.h"

#include "p_fix.h"


//
// FLOORS
//

//
// Move a plane (floor or ceiling) and check for crushing
//
result_e T_MovePlane(sector_t *sector, fixed_t speed, fixed_t dest,
                     boolean crush, int floorOrCeiling, int direction)
{
    fixed_t     lastpos;
    fixed_t     destheight;

    switch (floorOrCeiling)
    {
        case 0:
            // FLOOR
            switch (direction)
            {
                case -1:
                    // DOWN
                    if (sector->floorheight - speed < dest)
                    {
                        lastpos = sector->floorheight;
                        sector->floorheight = dest;
                        P_ChangeSector(sector, crush);
                        return pastdest;
                    }
                    else
                    {
                        lastpos = sector->floorheight;
                        sector->floorheight -= speed;
                        P_ChangeSector(sector, crush);
                    }
                    break;

                case 1:
                    // UP
                    destheight = (dest < sector->ceilingheight ? dest : sector->ceilingheight);
                    if (sector->floorheight + speed > destheight)
                    {
                        lastpos = sector->floorheight;
                        sector->floorheight = dest;
                        if (P_ChangeSector(sector, crush))
                        {
                            sector->floorheight = lastpos;
                            P_ChangeSector(sector, crush);
                        }
                        return pastdest;
                    }
                    else
                    {
                        // COULD GET CRUSHED
                        lastpos = sector->floorheight;
                        sector->floorheight += speed;
                        if (P_ChangeSector(sector, crush))
                        {
                            sector->floorheight = lastpos;
                            P_ChangeSector(sector, crush);
                            return crushed;
                        }
                    }
                    break;
            }
            break;

        case 1:
            // CEILING
            switch (direction)
            {
                case -1:
                    // DOWN
                    destheight = (dest > sector->floorheight ? dest : sector->floorheight);
                    if (sector->ceilingheight - speed < destheight)
                    {
                        lastpos = sector->ceilingheight;
                        sector->ceilingheight = dest;
                        if (P_ChangeSector(sector, crush))
                        {
                            sector->ceilingheight = lastpos;
                            P_ChangeSector(sector, crush);
                        }
                        return pastdest;
                    }
                    else
                    {
                        // COULD GET CRUSHED
                        lastpos = sector->ceilingheight;
                        sector->ceilingheight -= speed;
                        if (P_ChangeSector(sector, crush))
                        {
                            if (crush)
                                return crushed;
                            sector->ceilingheight = lastpos;
                            P_ChangeSector(sector, crush);
                            return crushed;
                        }
                    }
                    break;

                case 1:
                    // UP
                    if (sector->ceilingheight + speed > dest)
                    {
                        lastpos = sector->ceilingheight;
                        sector->ceilingheight = dest;
                        P_ChangeSector(sector, crush);
                        return pastdest;
                    }
                    else
                    {
                        lastpos = sector->ceilingheight;
                        sector->ceilingheight += speed;
                        P_ChangeSector(sector, crush);
                    }
                    break;
            }
            break;
    }
    return ok;
}


//
// MOVE A FLOOR TO IT'S DESTINATION (UP OR DOWN)
//
void T_MoveFloor(floormove_t *floor)
{
    result_e    res;

    res = T_MovePlane(floor->sector,
                      floor->speed,
                      floor->floordestheight,
                      floor->crush, 0, floor->direction);

    if (!(leveltime & 7)
        && floor->sector->floorheight != floor->floordestheight)
        S_StartSound((mobj_t *)&floor->sector->soundorg, sfx_stnmov);

    if (res == pastdest)
    {
        floor->sector->specialdata = NULL;

        if (floor->direction == 1)
        {
            switch (floor->type)
            {
                case donutRaise:
                    floor->sector->special = floor->newspecial;
                    floor->sector->floorpic = floor->texture;
                default:
                    break;
            }
        }
        else if (floor->direction == -1)
        {
            switch (floor->type)
            {
                case lowerAndChange:
                    floor->sector->special = floor->newspecial;
                    floor->sector->floorpic = floor->texture;
                default:
                    break;
            }
        }
        P_RemoveThinker(&floor->thinker);

        if (floor->stopsound)
            S_StartSound((mobj_t *)&floor->sector->soundorg, sfx_pstop);
    }
}

extern boolean  canmodify;

//
// HANDLE FLOOR TYPES
//
int EV_DoFloor(line_t *line, floor_e floortype)
{
    int                 secnum;
    int                 rtn;
    int                 i;
    floormove_t         *floor;

    secnum = -1;
    rtn = 0;
    while ((secnum = P_FindSectorFromLineTag(line, secnum)) >= 0)
    {
        sector_t        *sec;

        sec = &sectors[secnum];

        // ALREADY MOVING? IF SO, KEEP GOING...
        if (sec->specialdata)
            continue;

        // new floor thinker
        rtn = 1;
        floor = (floormove_t *)Z_Malloc(sizeof(*floor), PU_LEVSPEC, 0);
        P_AddThinker(&floor->thinker);
        sec->specialdata = floor;
        floor->thinker.function.acp1 = (actionf_p1)T_MoveFloor;
        floor->type = floortype;
        floor->crush = false;

        switch (floortype)
        {
            case lowerFloor:
                floor->direction = -1;
                floor->sector = sec;
                floor->speed = FLOORSPEED;
                floor->floordestheight =
                    P_FindHighestFloorSurrounding(sec);
                break;

            case lowerFloorToLowest:
                floor->direction = -1;
                floor->sector = sec;
                floor->speed = FLOORSPEED;
                floor->floordestheight =
                    P_FindLowestFloorSurrounding(sec);
                break;

            case turboLower:
                floor->direction = -1;
                floor->sector = sec;
                floor->speed = FLOORSPEED * 4;
                floor->floordestheight =
                    P_FindHighestFloorSurrounding(sec);
                if (floor->floordestheight != sec->floorheight)
                    floor->floordestheight += 8 * FRACUNIT;
                break;

            case raiseFloorCrush:
                floor->crush = true;

            case raiseFloor:
                floor->direction = 1;
                floor->sector = sec;
                floor->speed = FLOORSPEED;
                floor->floordestheight =
                    P_FindLowestCeilingSurrounding(sec);
                if (floor->floordestheight > sec->ceilingheight)
                    floor->floordestheight = sec->ceilingheight;
                floor->floordestheight -= (8 * FRACUNIT) *
                    (floortype == raiseFloorCrush);
                break;

            case raiseFloorTurbo:
                floor->direction = 1;
                floor->sector = sec;
                floor->speed = FLOORSPEED * 4;
                floor->floordestheight =
                    P_FindNextHighestFloor(sec, sec->floorheight);
                break;

            case raiseFloorToNearest:
                floor->direction = 1;
                floor->sector = sec;
                floor->speed = FLOORSPEED;
                floor->floordestheight =
                    P_FindNextHighestFloor(sec, sec->floorheight);
                break;

            case raiseFloor24:
                floor->direction = 1;
                floor->sector = sec;
                floor->speed = FLOORSPEED;
                floor->floordestheight = floor->sector->floorheight +
                    24 * FRACUNIT;
                break;

            case raiseFloor512:
                floor->direction = 1;
                floor->sector = sec;
                floor->speed = FLOORSPEED;
                floor->floordestheight = floor->sector->floorheight +
                    512 * FRACUNIT;
                break;

            case raiseFloor24AndChange:
                floor->direction = 1;
                floor->sector = sec;
                floor->speed = FLOORSPEED;
                floor->floordestheight = floor->sector->floorheight +
                    24 * FRACUNIT;

                if (E2M2)
                    sec->floorpic = R_FlatNumForName("FLOOR5_4");
                else if (MAP12)
                    sec->floorpic = R_FlatNumForName("FLOOR7_1");
                else
                    sec->floorpic = line->frontsector->floorpic;

                sec->special = line->frontsector->special;
                break;

            case raiseToTexture:
            {
                int minsize = 32000 << FRACBITS;

                floor->direction = 1;
                floor->sector = sec;
                floor->speed = FLOORSPEED;
                for (i = 0; i < sec->linecount; i++)
                {
                    if (twoSided(secnum, i))
                    {
                        side_t *side = getSide(secnum, i, 0);
                        if (side->bottomtexture > 0
                            && textureheight[side->bottomtexture] < minsize)
                            minsize = textureheight[side->bottomtexture];
                        side = getSide(secnum, i, 1);
                        if (side->bottomtexture > 0
                            && textureheight[side->bottomtexture] < minsize)
                            minsize = textureheight[side->bottomtexture];
                    }
                }
                floor->floordestheight =
                    (floor->sector->floorheight >> FRACBITS) + (minsize >> FRACBITS);

                if (floor->floordestheight > 32000)
                    floor->floordestheight = 32000;
                floor->floordestheight <<= FRACBITS;
            }
            break;

        case lowerAndChange:
            floor->direction = -1;
            floor->sector = sec;
            floor->speed = FLOORSPEED;
            floor->floordestheight = P_FindLowestFloorSurrounding(sec);
            floor->texture = sec->floorpic;
            floor->newspecial = sec->special;

            for (i = 0; i < sec->linecount; i++)
            {
                if (twoSided(secnum, i))
                {
                    if (getSide(secnum,i,0)->sector - sectors == secnum)
                    {
                        sec = getSector(secnum, i, 1);

                        if (sec->floorheight == floor->floordestheight)
                        {
                            floor->texture = sec->floorpic;
                            floor->newspecial = sec->special;
                            break;
                        }
                    }
                    else
                    {
                        sec = getSector(secnum,i,0);

                        if (sec->floorheight == floor->floordestheight)
                        {
                            floor->texture = sec->floorpic;
                            floor->newspecial = sec->special;
                            break;
                        }
                    }
                }
            }

        default:
            break;
        }

        floor->stopsound = (floor->sector->floorheight != floor->floordestheight);

        for (i = 0; i < sec->linecount; i++)
            sec->lines[i]->flags &= ~ML_SECRET;
    }
    return rtn;
}




//
// BUILD A STAIRCASE!
//
int EV_BuildStairs(line_t *line, stair_e type)
{
    int                 secnum;
    int                 height;
    int                 i;
    int                 newsecnum;
    int                 texture;
    int                 ok;
    int                 rtn;

    sector_t            *sec;
    sector_t            *tsec;

    floormove_t         *floor;

    fixed_t             stairsize = 0;
    fixed_t             speed = 0;

    boolean             crushing = false;

    secnum = -1;
    rtn = 0;
    while ((secnum = P_FindSectorFromLineTag(line, secnum)) >= 0)
    {
        sec = &sectors[secnum];

        // ALREADY MOVING?  IF SO, KEEP GOING...
        if (sec->specialdata)
            continue;

        // new floor thinker
        rtn = 1;
        floor = (floormove_t *)Z_Malloc(sizeof(*floor), PU_LEVSPEC, 0);
        P_AddThinker(&floor->thinker);
        sec->specialdata = floor;
        floor->thinker.function.acp1 = (actionf_p1)T_MoveFloor;
        floor->direction = 1;
        floor->sector = sec;
        switch (type)
        {
            case build8:
                speed = FLOORSPEED / 4;
                stairsize = 8 * FRACUNIT;
                crushing = false;
                break;
            case turbo16:
                speed = FLOORSPEED * 4;
                stairsize = 16 * FRACUNIT;
                crushing = true;
                break;
        }
        floor->speed = speed;
        height = sec->floorheight + stairsize;
        floor->floordestheight = height;
        floor->newspecial = 0;
        floor->texture = 0;
        floor->crush = crushing;
        floor->type = buildStair;
        floor->stopsound = (floor->sector->floorheight != floor->floordestheight);

        texture = sec->floorpic;

        // Find next sector to raise
        // 1. Find 2-sided line with same sector side[0]
        // 2. Other side is the next sector to raise
        do
        {
            ok = 0;
            for (i = 0; i < sec->linecount; i++)
            {
                if (!((sec->lines[i])->flags & ML_TWOSIDED))
                    continue;

                tsec = (sec->lines[i])->frontsector;
                newsecnum = tsec - sectors;

                if (secnum != newsecnum)
                    continue;

                tsec = (sec->lines[i])->backsector;
                if (!tsec)
                    continue;
                newsecnum = tsec - sectors;

                if (tsec->floorpic != texture)
                    continue;

                height += stairsize;

                if (tsec->specialdata)
                    continue;

                sec = tsec;
                secnum = newsecnum;
                floor = (floormove_t *)Z_Malloc(sizeof(*floor), PU_LEVSPEC, 0);

                P_AddThinker(&floor->thinker);

                sec->specialdata = floor;
                floor->thinker.function.acp1 = (actionf_p1)T_MoveFloor;
                floor->direction = 1;
                floor->sector = sec;
                floor->speed = speed;
                floor->floordestheight = height;
                floor->stopsound = (floor->sector->floorheight != floor->floordestheight);
                ok = 1;
                break;
            }
        } while (ok);
    }
    return rtn;
}