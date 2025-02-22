/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * CursorPositionProcess()
 * TagProcess()
 * PointProcess()
 */

#include "common/coroutines.h"
#include "tinsel/actors.h"
#include "tinsel/background.h"
#include "tinsel/cursor.h"
#include "tinsel/dw.h"
#include "tinsel/events.h"
#include "tinsel/font.h"
#include "tinsel/graphics.h"
#include "tinsel/multiobj.h"
#include "tinsel/object.h"
#include "tinsel/pcode.h"
#include "tinsel/polygons.h"
#include "tinsel/movers.h"
#include "tinsel/sched.h"
#include "tinsel/strres.h"
#include "tinsel/text.h"
#include "tinsel/tinsel.h"

#include "common/textconsole.h"

namespace Tinsel {

//----------------- EXTERNAL GLOBAL DATA --------------------

#ifdef DEBUG
//extern int Overrun;		// The overrun counter, in DOS_DW.C

extern int g_newestString;	// The overrun counter, in STRRES.C
#endif


//----------------- LOCAL DEFINES --------------------

#define LPOSX	295		// X-co-ord of lead actor's position display
#define CPOSX	24		// X-co-ord of cursor's position display
#define OPOSX	SCRN_CENTER_X	// X-co-ord of overrun counter's display
#define SPOSX	SCRN_CENTER_X	// X-co-ord of string numbner's display

#define POSY	0		// Y-co-ord of these position displays

enum HotSpotTag {
	NO_HOTSPOT_TAG,
	POLY_HOTSPOT_TAG,
	ACTOR_HOTSPOT_TAG
};

//----------------- LOCAL GLOBAL DATA --------------------

// These vars are reset upon engine destruction

static bool g_DispPath = false;
static bool g_bShowString = false;

static int	g_TaggedActor = 0;
static HPOLYGON	g_hTaggedPolygon = NOPOLY;

static bool g_bTagsActive = true;

static bool g_bPointingActive = true;
static int tagX = 0, tagY = 0; // Values when tag was displayed
static int Loffset = 0, Toffset = 0; // Values when tag was displayed
static int curX = 0, curY = 0;

void ResetVarsPDisplay() {
	g_DispPath = false;
	g_bShowString = false;

	g_TaggedActor = 0;
	g_hTaggedPolygon = NOPOLY;

	g_bTagsActive = true;

	g_bPointingActive = true;

	tagX = tagY = 0;
	Loffset = Toffset = 0;
	curX = curY = 0;
}

#ifdef DEBUG
/**
 * Displays the cursor and lead actor's co-ordinates and the overrun
 * counter. Also which path polygon the cursor is in, if required.
 *
 * This process is only started up if a Glitter showpos() call is made.
 * Obviously, this is for testing purposes only...
 */
void CursorPositionProcess(CORO_PARAM, const void *) {
	// COROUTINE
	CORO_BEGIN_CONTEXT;
		int prevsX, prevsY;	// Last screen top left
		int prevcX, prevcY;	// Last displayed cursor position
		int prevlX, prevlY;	// Last displayed lead actor position
//		int prevOver;		// Last displayed overrun
		int prevString;		// Last displayed string number

		OBJECT *cpText;		// cursor position text object pointer
		OBJECT *cpathText;	// cursor path text object pointer
		OBJECT *rpText;		// text object pointer
//		OBJECT *opText;		// text object pointer
		OBJECT *spText;		// string number text object pointer
	CORO_END_CONTEXT(_ctx);

	CORO_BEGIN_CODE(_ctx);

	_ctx->prevsX = -1;
	_ctx->prevsY = -1;
	_ctx->prevcX = -1;
	_ctx->prevcY = -1;
	_ctx->prevlX = -1;
	_ctx->prevlY = -1;
//	_ctx->prevOver = -1;
	_ctx->prevString = -1;

	_ctx->cpText = nullptr;
	_ctx->cpathText = nullptr;
	_ctx->rpText = nullptr;
//	_ctx->opText = nullptr;
	_ctx->spText = nullptr;


	int aniX, aniY;			// cursor/lead actor position
	int Loffset, Toffset;		// Screen top left

	char PositionString[64];	// sprintf() things into here

	MOVER *pActor;		// Lead actor

	while (1) {
		_vm->_bg->PlayfieldGetPos(FIELD_WORLD, &Loffset, &Toffset);

		/*-----------------------------------*\
		| Cursor's position and path display. |
		\*-----------------------------------*/
		_vm->_cursor->GetCursorXY(&aniX, &aniY, false);

		// Change in cursor position?
		if (aniX != _ctx->prevcX || aniY != _ctx->prevcY ||
				Loffset != _ctx->prevsX || Toffset != _ctx->prevsY) {
			// kill current text objects
			if (_ctx->cpText) {
				MultiDeleteObject(_vm->_bg->GetPlayfieldList(FIELD_STATUS), _ctx->cpText);
			}
			if (_ctx->cpathText) {
				MultiDeleteObject(_vm->_bg->GetPlayfieldList(FIELD_STATUS), _ctx->cpathText);
				_ctx->cpathText = nullptr;
			}

			// New text objects
			sprintf(PositionString, "%d %d", aniX + Loffset, aniY + Toffset);
			_ctx->cpText = ObjectTextOut(_vm->_bg->GetPlayfieldList(FIELD_STATUS), PositionString,
						0, CPOSX, POSY, _vm->_font->GetTagFontHandle(), TXT_CENTER);
			if (g_DispPath) {
				HPOLYGON hp = InPolygon(aniX + Loffset, aniY + Toffset, PATH);
				if (hp == NOPOLY)
					sprintf(PositionString, "No path");
				else
					sprintf(PositionString, "%d,%d %d,%d %d,%d %d,%d",
						PolyCornerX(hp, 0), PolyCornerY(hp, 0),
						PolyCornerX(hp, 1), PolyCornerY(hp, 1),
						PolyCornerX(hp, 2), PolyCornerY(hp, 2),
						PolyCornerX(hp, 3), PolyCornerY(hp, 3));
				_ctx->cpathText = ObjectTextOut(_vm->_bg->GetPlayfieldList(FIELD_STATUS), PositionString,
							0, 4, POSY+ 10, _vm->_font->GetTagFontHandle(), 0);
			}

			// update previous position
			_ctx->prevcX = aniX;
			_ctx->prevcY = aniY;
		}

#if 0
		/*------------------------*\
		| Overrun counter display. |
		\*------------------------*/
		if (Overrun != _ctx->prevOver) {
			// kill current text objects
			if (_ctx->opText) {
				MultiDeleteObject(_vm->_bg->GetPlayfieldList(FIELD_STATUS), _ctx->opText);
			}

			sprintf(PositionString, "%d", Overrun);
			_ctx->opText = ObjectTextOut(_vm->_bg->GetPlayfieldList(FIELD_STATUS), PositionString,
						0, OPOSX, POSY, GetTagFontHandle(), TXT_CENTER);

			// update previous value
			_ctx->prevOver = Overrun;
		}
#endif

		/*----------------------*\
		| Lead actor's position. |
		\*----------------------*/
		pActor = GetMover(LEAD_ACTOR);
		if (pActor && getMActorState(pActor)) {
			// get lead's animation position
			_vm->_actor->GetActorPos(LEAD_ACTOR, &aniX, &aniY);

			// Change in position?
			if (aniX != _ctx->prevlX || aniY != _ctx->prevlY ||
					Loffset != _ctx->prevsX || Toffset != _ctx->prevsY) {
				// Kill current text objects
				if (_ctx->rpText) {
					MultiDeleteObject(_vm->_bg->GetPlayfieldList(FIELD_STATUS), _ctx->rpText);
				}

				// create new text object list
				sprintf(PositionString, "%d %d", aniX, aniY);
				_ctx->rpText = ObjectTextOut(_vm->_bg->GetPlayfieldList(FIELD_STATUS), PositionString,
								0, LPOSX, POSY,	_vm->_font->GetTagFontHandle(), TXT_CENTER);

				// update previous position
				_ctx->prevlX = aniX;
				_ctx->prevlY = aniY;
			}
		}

		/*-------------*\
		| String number	|
		\*-------------*/
		if (g_bShowString && g_newestString != _ctx->prevString) {
			// kill current text objects
			if (_ctx->spText) {
				MultiDeleteObject(_vm->_bg->GetPlayfieldList(FIELD_STATUS), _ctx->spText);
			}

			sprintf(PositionString, "String: %d", g_newestString);
			_ctx->spText = ObjectTextOut(_vm->_bg->GetPlayfieldList(FIELD_STATUS), PositionString,
						0, SPOSX, POSY+10, _vm->_font->GetTalkFontHandle(), TXT_CENTER);

			// update previous value
			_ctx->prevString = g_newestString;
		}

		// update previous playfield position
		_ctx->prevsX = Loffset;
		_ctx->prevsY = Toffset;

		CORO_SLEEP(1);		// allow re-scheduling
	}
	CORO_END_CODE;
}
#endif

/**
 * While inventory/menu is open.
 */
void DisablePointing() {
	int	i;
	HPOLYGON hPoly;		// Polygon handle

	g_bPointingActive = false;

	for (i = 0; i < MAX_POLY; i++)	{
		hPoly = GetPolyHandle(i);

		if (hPoly != NOPOLY && PolyType(hPoly) == TAG && PolyIsPointedTo(hPoly)) {
			SetPolyPointedTo(hPoly, false);
			SetPolyTagWanted(hPoly, false, false, 0);
			PolygonEvent(Common::nullContext, hPoly, UNPOINT, 0, false, 0);
		}
	}

	// For each tagged actor
	for (i = 0; (i = _vm->_actor->NextTaggedActor(i)) != 0;) {
		if (_vm->_actor->ActorIsPointedTo(i)) {
			_vm->_actor->SetActorPointedTo(i, false);
			_vm->_actor->SetActorTagWanted(i, false, false, 0);

			ActorEvent(Common::nullContext, i, UNPOINT, false, 0);
		}
	}
}

/**
 * EnablePointing()
 */
void EnablePointing() {
	g_bPointingActive = true;
}

/**
 * Tag process keeps us updated as to which tagged actor is currently tagged
 * (if one is). Tag process asks us for this information, as does ProcessUserEvent().
 */
static void SaveTaggedActor(int ano) {
	g_TaggedActor = ano;
}

/**
 * Tag process keeps us updated as to which tagged actor is currently tagged
 * (if one is). Tag process asks us for this information, as does ProcessUserEvent().
 */
int GetTaggedActor() {
	return g_TaggedActor;
}

/**
 * Tag process keeps us updated as to which polygon is currently tagged
 * (if one is). Tag process asks us for this information, as does ProcessUserEvent().
 */
static void SaveTaggedPoly(HPOLYGON hp) {
	g_hTaggedPolygon = hp;
}

HPOLYGON GetTaggedPoly() {
	return g_hTaggedPolygon;
}

/**
 * Given cursor position and an actor number, ascertains whether the
 * cursor is within the actor's tag area.
 * Returns TRUE for a positive result, FALSE for negative.
 * If TRUE, the mid-top co-ordinates of the actor's tag area are also
 * returned.
 */
static bool InHotSpot(int ano, int aniX, int aniY, int *pxtext, int *pytext) {
	int	Top, Bot;		// Top and bottom limits of active area
	int	left, right;	// left and right of active area
	int	qrt = 0;		// 1/4 of height (sometimes 1/2)

	// First check if within x-range
	if (aniX > (left = _vm->_actor->GetActorLeft(ano)) && aniX < (right = _vm->_actor->GetActorRight(ano))) {
		Top = _vm->_actor->GetActorTop(ano);
		Bot = _vm->_actor->GetActorBottom(ano);

		// y-range varies according to tag-type
		switch (_vm->_actor->TagType(ano)) {
		case TAG_DEF:
			// Next to bottom 1/4 of the actor's area
			qrt = (Bot - Top) >> 1;		// Half actor's height
			Top += qrt;			// Top = mid-height

			qrt = qrt >> 1;			// Quarter height
			Bot -= qrt;			// Bot = 1/4 way up
			break;

		case TAG_Q1TO3:
			// Top 3/4 of the actor's area
			qrt = (Bot - Top) >> 2;		// 1/4 actor's height
			Bot -= qrt;			// Bot = 1/4 way up
			break;

		case TAG_Q1TO4:
			// All the actor's area
			break;

		default:
			error("illegal tag area type");
		}

		// Now check if within y-range
		if (aniY >= Top && aniY <= Bot) {
			if (_vm->_actor->TagType(ano) == TAG_Q1TO3)
				*pytext = Top + qrt;
			else
				*pytext = Top;
			*pxtext = (left + right) / 2;
			return true;
		}
	}
	return false;
}

/**
 * See if the cursor is over a tagged actor's hot-spot. If so, display
 * the tag or, if tag already displayed, maintain the tag's position on
 * the screen.
 */
static bool ActorTag(int curX_, int curY_, HotSpotTag *pTag, OBJECT **ppText) {
	int	newX, newY;		// new values, to keep tag in place
	int	ano;
	int	xtext, ytext;
	bool	newActor;
	char tagBuffer[64];

	if (TinselVersion >= 2) {
		// Tinsel 2 version
		// Get the foremost pointed to actor
		int actor = _vm->_actor->FrontTaggedActor();

		if (actor == 0) {
			SaveTaggedActor(0);
			return false;
		}

		// If new actor
		// or actor has suddenly decided it wants tagging...
		if (actor != GetTaggedActor() || (_vm->_actor->ActorTagIsWanted(actor) && !*ppText)) {
			// Put up actor tag
			SaveTaggedActor(actor);		// This actor tagged
			SaveTaggedPoly(NOPOLY);		// No tagged polygon

			MultiDeleteObjectIfExists(FIELD_STATUS, ppText);

			if (_vm->_actor->ActorTagIsWanted(actor)) {
				_vm->_actor->GetActorTagPos(actor, &tagX, &tagY, false);
				LoadStringRes(_vm->_actor->GetActorTagHandle(actor), tagBuffer, sizeof(tagBuffer));

				// May have buggered cursor
				_vm->_cursor->EndCursorFollowed();
				*ppText = ObjectTextOut(_vm->_bg->GetPlayfieldList(FIELD_STATUS), tagBuffer,
						0, tagX, tagY, _vm->_font->GetTagFontHandle(), TXT_CENTER, 0);
				assert(*ppText);
				MultiSetZPosition(*ppText, Z_TAG_TEXT);
			} else
				*ppText = nullptr;
		} else if (*ppText) {
			// Same actor, maintain tag position
			_vm->_actor->GetActorTagPos(actor, &newX, &newY, false);

			if (newX != tagX || newY != tagY) {
				MultiMoveRelXY(*ppText, newX - tagX, newY - tagY);
				tagX = newX;
				tagY = newY;
			}
		}

		return true;
	}

	// Tinsel 1 version
	// For each actor with a tag....
	_vm->_actor->FirstTaggedActor();
	while ((ano = _vm->_actor->NextTaggedActor()) != 0) {
		if (InHotSpot(ano, curX_, curY_, &xtext, &ytext)) {
			// Put up or maintain actor tag
			if (*pTag != ACTOR_HOTSPOT_TAG)
				newActor = true;
			else if (ano != GetTaggedActor())
				newActor = true;	// Different actor
			else
				newActor = false;	// Same actor

			if (newActor) {
				// Display actor's tag

				MultiDeleteObjectIfExists(FIELD_STATUS, ppText);

				*pTag = ACTOR_HOTSPOT_TAG;
				SaveTaggedActor(ano);	// This actor tagged
				SaveTaggedPoly(NOPOLY);	// No tagged polygon

				_vm->_bg->PlayfieldGetPos(FIELD_WORLD, &tagX, &tagY);
				LoadStringRes(_vm->_actor->GetActorTag(ano), _vm->_font->TextBufferAddr(), TBUFSZ);
				*ppText = ObjectTextOut(_vm->_bg->GetPlayfieldList(FIELD_STATUS), _vm->_font->TextBufferAddr(),
							0, xtext - tagX, ytext - tagY, _vm->_font->GetTagFontHandle(), TXT_CENTER);
				assert(*ppText); // Actor tag string produced NULL text
				MultiSetZPosition(*ppText, Z_TAG_TEXT);
			} else {
				// Maintain actor tag's position

				_vm->_bg->PlayfieldGetPos(FIELD_WORLD, &newX, &newY);
				if (newX != tagX || newY != tagY) {
					MultiMoveRelXY(*ppText, tagX - newX, tagY - newY);
					tagX = newX;
					tagY = newY;
				}
			}
			return true;
		}
	}

	// No tagged actor
	if (*pTag == ACTOR_HOTSPOT_TAG) {
		*pTag = NO_HOTSPOT_TAG;
		SaveTaggedActor(0);
	}
	return false;
}

/**
 * Perhaps some comment in due course.
 *
 * Under control of PointProcess(), when the cursor is over a TAG or
 * EXIT polygon, its pointState flag is set to POINTING. If its Glitter
 * code contains a printtag() call, its tagState flag gets set to TAG_ON.
 */
static bool PolyTag(HotSpotTag *pTag, OBJECT **ppText) {
	int		nLoff, nToff;		// new values, to keep tag in place
	HPOLYGON	hp;
	bool	newPoly;
	int	shift;

	int	tagx, tagy;	// Tag display co-ordinates
	SCNHANDLE hTagtext;	// Tag text

	// For each polgon with a tag....
	for (int i = 0; i < MAX_POLY; i++) {
		hp = GetPolyHandle(i);

		if ((TinselVersion >= 2) && (hp == NOPOLY))
			continue;

		// Added code for un-tagged tags
		if ((hp != NOPOLY) && (PolyPointState(hp) == PS_POINTING) && (PolyTagState(hp) != TAG_ON)) {
			// This poly is entitled to be tagged
			if (hp != GetTaggedPoly()) {
				MultiDeleteObjectIfExists(FIELD_STATUS, ppText);
				*pTag = POLY_HOTSPOT_TAG;
				SaveTaggedActor(0);	// No tagged actor
				SaveTaggedPoly(hp);	// This polygon tagged
			}
			return true;
		} else if (((TinselVersion >= 2) && PolyTagIsWanted(hp)) ||
			((TinselVersion <= 1) && hp != NOPOLY && PolyTagState(hp) == TAG_ON)) {
			// Put up or maintain polygon tag
			newPoly = false;
			if (TinselVersion >= 2) {
				if (hp != GetTaggedPoly())
					newPoly = true;		// Different polygon
			} else {
				if (*pTag != POLY_HOTSPOT_TAG)
					newPoly = true;		// A new polygon (no current)
				else if (hp != GetTaggedPoly())
					newPoly = true;		// Different polygon
			}

			if (newPoly) {
				if (*ppText)
					MultiDeleteObject(_vm->_bg->GetPlayfieldList(FIELD_STATUS), *ppText);

				if (TinselVersion <= 1)
					*pTag = POLY_HOTSPOT_TAG;
				SaveTaggedActor(0);	// No tagged actor
				SaveTaggedPoly(hp);	// This polygon tagged

				_vm->_bg->PlayfieldGetPos(FIELD_WORLD, &Loffset, &Toffset);
				GetTagTag(hp, &hTagtext, &tagx, &tagy);

				int strLen;
				if (GetPolyTagHandle(hp) != 0)
					strLen = LoadStringRes(GetPolyTagHandle(hp), _vm->_font->TextBufferAddr(), TBUFSZ);
				else
					strLen = LoadStringRes(hTagtext, _vm->_font->TextBufferAddr(), TBUFSZ);

				if (strLen == 0)
					// No valid string returned, so leave ppText as NULL
					ppText = nullptr;
				else if ((TinselVersion >= 2) && !PolyTagFollowsCursor(hp)) {
					// May have buggered cursor
					_vm->_cursor->EndCursorFollowed();

					*ppText = ObjectTextOut(_vm->_bg->GetPlayfieldList(FIELD_STATUS),
							_vm->_font->TextBufferAddr(), 0, tagx - Loffset, tagy - Toffset,
							_vm->_font->GetTagFontHandle(), TXT_CENTER, 0);
				} else if (TinselVersion >= 2) {
					// Bugger cursor
					const char *tagPtr = _vm->_font->TextBufferAddr();
					if (tagPtr[0] < ' ' && tagPtr[1] == EOS_CHAR)
						_vm->_cursor->StartCursorFollowed();

					_vm->_cursor->GetCursorXYNoWait(&curX, &curY, false);
					*ppText = ObjectTextOut(_vm->_bg->GetPlayfieldList(FIELD_STATUS), _vm->_font->TextBufferAddr(),
							0, curX, curY, _vm->_font->GetTagFontHandle(), TXT_CENTER, 0);
				} else {
					// Handle displaying the tag text on-screen
					*ppText = ObjectTextOut(_vm->_bg->GetPlayfieldList(FIELD_STATUS), _vm->_font->TextBufferAddr(),
							0, tagx - Loffset, tagy - Toffset,
						_vm->_font->GetTagFontHandle(), TXT_CENTER);
					assert(*ppText); // Polygon tag string produced NULL text
				}

				// DW1 has some tags without text, e.g. the "equals" button when talking to the guard in act 3
				if (ppText) {
					MultiSetZPosition(*ppText, Z_TAG_TEXT);

					/*
					* New feature: Don't go off the side of the background
					*/
					shift = MultiRightmost(*ppText) + Loffset + 2;
					if (shift >= _vm->_bg->BgWidth())			// Not off right
						MultiMoveRelXY(*ppText, _vm->_bg->BgWidth() - shift, 0);
					shift = MultiLeftmost(*ppText) + Loffset - 1;
					if (shift <= 0)					// Not off left
						MultiMoveRelXY(*ppText, -shift, 0);
					shift = MultiLowest(*ppText) + Toffset;
					if (shift > _vm->_bg->BgHeight())			// Not off bottom
						MultiMoveRelXY(*ppText, 0, _vm->_bg->BgHeight() - shift);
				}
			} else if ((TinselVersion >= 2) && (*ppText)) {
				if (!PolyTagFollowsCursor(hp)) {
					_vm->_bg->PlayfieldGetPos(FIELD_WORLD, &nLoff, &nToff);
					if (nLoff != Loffset || nToff != Toffset) {
						MultiMoveRelXY(*ppText, Loffset - nLoff, Toffset - nToff);
						Loffset = nLoff;
						Toffset = nToff;
					}
				} else {
					_vm->_cursor->GetCursorXY(&tagx, &tagy, false);
					if (tagx != curX || tagy != curY) {
						MultiMoveRelXY(*ppText, tagx - curX, tagy - curY);
						curX = tagx;
						curY = tagy;
					}
				}
			} else if (TinselVersion <= 1) {
				_vm->_bg->PlayfieldGetPos(FIELD_WORLD, &nLoff, &nToff);
				if (nLoff != Loffset || nToff != Toffset) {
					MultiMoveRelXY(*ppText, Loffset - nLoff, Toffset - nToff);
					Loffset = nLoff;
					Toffset = nToff;
				}
			}
			return true;
		}
	}

	// No tagged polygon
	if (TinselVersion >= 2)
		SaveTaggedPoly(NOPOLY);
	else if (*pTag == POLY_HOTSPOT_TAG) {
		*pTag = NO_HOTSPOT_TAG;
		SaveTaggedPoly(NOPOLY);
	}
	return false;
}

/**
 * Handle display of tagged actor and polygon tags.
 * Tagged actor's get priority over polygons.
 */
void TagProcess(CORO_PARAM, const void *) {
	// COROUTINE
	CORO_BEGIN_CONTEXT;
		OBJECT	*pText;	// text object pointer
		HotSpotTag Tag;
	CORO_END_CONTEXT(_ctx);

	CORO_BEGIN_CODE(_ctx);

	_ctx->pText = nullptr;
	_ctx->Tag = NO_HOTSPOT_TAG;

	SaveTaggedActor(0);		// No tagged actor yet
	SaveTaggedPoly(NOPOLY);		// No tagged polygon yet

	while (1) {
		if (g_bTagsActive) {
			int	curX_, curY_;	// cursor position
			while (!_vm->_cursor->GetCursorXYNoWait(&curX_, &curY_, true))
				CORO_SLEEP(1);

			if (!ActorTag(curX_, curY_, &_ctx->Tag, &_ctx->pText)
					&& !PolyTag(&_ctx->Tag, &_ctx->pText)) {
				// Nothing tagged. Remove tag, if there is one
				if (_ctx->pText) {
					MultiDeleteObject(_vm->_bg->GetPlayfieldList(FIELD_STATUS), _ctx->pText);
					_ctx->pText = nullptr;

					if (TinselVersion >= 2)
						// May have buggered cursor
						_vm->_cursor->EndCursorFollowed();
				}
			}
		} else {
			SaveTaggedActor(0);
			SaveTaggedPoly(NOPOLY);

			// Remove tag, if there is one
			if (_ctx->pText) {
				// kill current text objects
				MultiDeleteObjectIfExists(FIELD_STATUS, &_ctx->pText);
				_ctx->Tag = NO_HOTSPOT_TAG;
			}
		}

		CORO_SLEEP(1);		// allow re-scheduling
	}

	CORO_END_CODE;
}

/**
 * Called from PointProcess() as appropriate.
 */
static void enteringpoly(CORO_PARAM, HPOLYGON hp) {
	CORO_BEGIN_CONTEXT;
	CORO_END_CONTEXT(_ctx);

	CORO_BEGIN_CODE(_ctx);

	SetPolyPointState(hp, PS_POINTING);

	if (TinselVersion >= 2)
		CORO_INVOKE_ARGS(PolygonEvent, (CORO_SUBCTX, hp, POINTED, 0, false, 0));
	else
		RunPolyTinselCode(hp, POINTED, PLR_NOEVENT, false);

	CORO_END_CODE;
}

/**
 * Called from PointProcess() as appropriate.
 */
static void leavingpoly(CORO_PARAM, HPOLYGON hp) {
	CORO_BEGIN_CONTEXT;
	CORO_END_CONTEXT(_ctx);

	CORO_BEGIN_CODE(_ctx);

	SetPolyPointState(hp, PS_NOT_POINTING);

	if (TinselVersion >= 2) {
		CORO_INVOKE_ARGS(PolygonEvent, (CORO_SUBCTX, hp, UNPOINT, 0, false, 0));
		SetPolyTagWanted(hp, false, false, 0);

	} else if (PolyTagState(hp) == TAG_ON) {
		// Delete this tag entry
		SetPolyTagState(hp, TAG_OFF);
	}

	CORO_END_CODE;
}

/**
 * For TAG and EXIT polygons, monitor cursor entering and leaving.
 * Maintain the polygons' pointState and tagState flags accordingly.
 * Also run the polygon's Glitter code when the cursor enters.
 */
void PointProcess(CORO_PARAM, const void *) {
	// COROUTINE
	CORO_BEGIN_CONTEXT;
		HPOLYGON hPoly;
		int i;
		int curX, curY;	// cursor/tagged actor position
	CORO_END_CONTEXT(_ctx);

	CORO_BEGIN_CODE(_ctx);

	if (TinselVersion >= 2)
		EnablePointing();

	while (1) {
		while (!_vm->_cursor->GetCursorXYNoWait(&_ctx->curX, &_ctx->curY, true))
			CORO_SLEEP(1);

		/*----------------------------------*\
		| For polygons of type TAG and EXIT. |
		\*----------------------------------*/
		for (_ctx->i = 0; _ctx->i < MAX_POLY; _ctx->i++) {
			_ctx->hPoly = GetPolyHandle(_ctx->i);
			if ((_ctx->hPoly == NOPOLY) || ((PolyType(_ctx->hPoly) != TAG) &&
				(PolyType(_ctx->hPoly) != EXIT)))
				continue;

			if (!PolyIsPointedTo(_ctx->hPoly)) {
				if (IsInPolygon(_ctx->curX, _ctx->curY, _ctx->hPoly)) {
					if (TinselVersion >= 2) {
						SetPolyPointedTo(_ctx->hPoly, true);
						CORO_INVOKE_ARGS(PolygonEvent, (CORO_SUBCTX, _ctx->hPoly, POINTED, 0, false, 0));
					} else {
						CORO_INVOKE_1(enteringpoly, _ctx->hPoly);
					}
				}
			} else {
				if (!IsInPolygon(_ctx->curX, _ctx->curY, _ctx->hPoly)) {
					if (TinselVersion >= 2) {
						SetPolyPointedTo(_ctx->hPoly, false);
						SetPolyTagWanted(_ctx->hPoly, false, false, 0);
						CORO_INVOKE_ARGS(PolygonEvent, (CORO_SUBCTX, _ctx->hPoly, UNPOINT, 0, false, 0));
					} else {
						CORO_INVOKE_1(leavingpoly, _ctx->hPoly);
					}
				}
			}
		}

		if (TinselVersion >= 2) {
			// For each tagged actor
			for (_ctx->i = 0; (_ctx->i = _vm->_actor->NextTaggedActor(_ctx->i)) != 0;) {
				if (!_vm->_actor->ActorIsPointedTo(_ctx->i)) {
					if (_vm->_actor->InHotSpot(_ctx->i, _ctx->curX, _ctx->curY)) {
						_vm->_actor->SetActorPointedTo(_ctx->i, true);
						CORO_INVOKE_ARGS(ActorEvent, (CORO_SUBCTX, _ctx->i, POINTED, false, 0));
					}
				} else {
					if (!_vm->_actor->InHotSpot(_ctx->i, _ctx->curX, _ctx->curY)) {
						_vm->_actor->SetActorPointedTo(_ctx->i, false);
						_vm->_actor->SetActorTagWanted(_ctx->i, false, false, 0);
						CORO_INVOKE_ARGS(ActorEvent, (CORO_SUBCTX, _ctx->i, UNPOINT, false, 0));
					}
				}
			}

			// allow re-scheduling
			do {
				CORO_SLEEP(1);
			} while (!g_bPointingActive);
		} else {
			// allow re-scheduling
			CORO_SLEEP(1);
		}
	}

	CORO_END_CODE;
}

void DisableTags() {
	g_bTagsActive = false;
}

void EnableTags() {
	g_bTagsActive = true;
}

bool DisableTagsIfEnabled() {
	if (g_bTagsActive) {
		DisableTags();
		return true;
	} else
		return false;
}

/**
 * For testing purposes only.
 * Causes CursorPositionProcess() to display, or not, the path that the
 * cursor is in.
 */
void TogglePathDisplay() {
	g_DispPath ^= 1;	// Toggle path display (XOR with true)
}


void setshowstring() {
	g_bShowString = true;
}

} // End of namespace Tinsel
