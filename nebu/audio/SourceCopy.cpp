#ifndef GLTRON_NO_SOUND

#include "audio/nebu_SourceCopy.h"

#include <assert.h>

namespace Sound {
  int SourceCopy::Mix(Uint8 *data, int len) {
    if(_source->_buffer == NULL) return 0;

    int volume = (int)(_source->GetVolume() * NEBU_MIX_MAXVOLUME);
    // fprintf(stderr, "playing copy sample at %d, position: %d\n", volume, _position);
    int buffersize = _source->_buffersize;
    Uint8* buffer = (Uint8*) _source->_buffer;
    
    assert(len < buffersize);
      
    if(len < buffersize - _position) {
      nebu_MixAudio(data, buffer + _position, len, volume);
      _position += len;
    } else { 
      nebu_MixAudio(data, buffer + _position,
		                 buffersize - _position, volume);
      len -= buffersize - _position;

	      if(_loop) {
	if(_loop != 255)
	  _loop--;

	_position = 0;
	nebu_MixAudio(data, buffer + _position, len, volume);
	_position += len;
      } else {
	_isPlaying = 0;
      }
    }
    return 1;
  }
}

#endif
