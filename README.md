# AudioId

Sound identification.

<!--
./audioid --visualize --labels data/on-single-off-on-flush-off.fixed.txt data/on-single-off-on-flush-off.wav --write-state state.ini --learn

./audioid --visualize --labels data/on-single-off-on-flush-off.fixed.txt data/on-single-off-on-flush-off.wav --state state.ini
-->

Uses:

* [miniaudio](https://miniaud.io/) - audio capture (and playback) library 
* [minfft](https://github.com/aimukhin/minfft) - minimalistic Fast Fourier Transform library
