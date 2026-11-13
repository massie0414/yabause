/* Fixed-width sound-state fields. Native endianness follows the YSS header. */
#ifndef SOUNDSTATE_IO_H
#define SOUNDSTATE_IO_H
#include "core.h"

/* fp and load belong to the transfer function. Never serialize pointers,
   compiler padding, or executable JIT code. */
#define SOUND_STATE_FIELD(field) do { \
   u32 sound_state_word; \
   if (load) { \
      if (fread(&sound_state_word, sizeof(sound_state_word), 1, fp) != 1) return -1; \
      (field) = sound_state_word; \
   } else { \
      sound_state_word = (u32)(field); \
      if (fwrite(&sound_state_word, sizeof(sound_state_word), 1, fp) != 1) return -1; \
   } \
} while (0)
#define SOUND_STATE_ARRAY(array) do { \
   unsigned int sound_state_index; \
   for (sound_state_index = 0; sound_state_index < sizeof(array) / sizeof((array)[0]); sound_state_index++) \
      SOUND_STATE_FIELD((array)[sound_state_index]); \
} while (0)
#endif
