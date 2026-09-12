# Asset provenance for the faithful release

The faithful preview retains the assets from this repository's `upstream-0.70`
reference. A byte-level Git comparison of `art/default`, `data`, and `music`
against that tag has no differences. This includes the original models, font
atlases and metadata, textures, WAV/OGG effects, and Impulse Tracker soundtrack.

The original credits name Andreas Umbach for GLTron; Nicolas Zimmermann,
Charles Babbage, Tracy Brown, Tyler Esselstrom, and Allen Bond for artwork;
Peter Hajba for music; and Damon Law for sound. The original README explicitly
credits **Revenge of Cats** to Peter Hajba (Skaven). The source does not provide
an author-to-file mapping for every texture or font; these are the inherited
credits, not newly asserted file-level authorship.

The program's GPL version 2-or-later notice remains in `README` and `COPYING`.
The bundled Lua 4.0.1 permission notice from `lua/include/lua.h` is also included
in binary packages as `licenses/LUA-COPYRIGHT.txt`. Original notices and credits
must accompany redistributions. A code license should not be described as a
new grant of ownership over third-party names or assets.

No TRON film soundtrack, film frames, or new third-party asset downloads have
been added for this preview. A generated logo experiment was rejected for
changing the original design and is not included in the source or binary
package. Optional sharper artwork remains a separate release item; any accepted
replacement must record its source and generation method here before shipping.
