# AudioId

Sound identification.

<!--
./audioid --visualize --labels data/on-single-off-on-flush-off.txt data/on-single-off-on-flush-off.wav --learn --write-state state.ini

./audioid --visualize --state state.ini data/on-single-off-on-flush-off.wav --labels data/on-single-off-on-flush-off.txt
-->

Uses:

* [miniaudio](https://miniaud.io/) - audio capture (and playback) library 
* [minfft](https://github.com/aimukhin/minfft) - minimalistic Fast Fourier Transform library
