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
 */

#ifndef TOON_MOVIE_H
#define TOON_MOVIE_H

#include "toon/toon.h"
#include "video/smk_decoder.h"

namespace Toon {

class SubtitleRenderer;

class ToonstruckSmackerDecoder : public Video::SmackerDecoder {
public:
	ToonstruckSmackerDecoder();

	bool loadStream(Common::SeekableReadStream *stream) override;
	bool isLowRes() { return _lowRes; }

protected:
	void handleAudioTrack(byte track, uint32 chunkSize, uint32 unpackedSize) override;
	SmackerVideoTrack *createVideoTrack(uint32 width, uint32 height, uint32 frameCount, const Common::Rational &frameRate, uint32 flags, uint32 signature) const override;

private:
	bool _lowRes;
};

class Movie {
public:
	Movie(ToonEngine *vm, ToonstruckSmackerDecoder *decoder);
	virtual ~Movie(void);

	void init() const;
	void play(const Common::String &video, int32 flags = 0);
	bool isPlaying() { return _playing; }

protected:
	void playVideo(bool isFirstIntroVideo);
	ToonEngine *_vm;
	ToonstruckSmackerDecoder *_decoder;
	bool _playing;
	SubtitleRenderer *_subtitle;
};

} // End of namespace Toon

#endif
