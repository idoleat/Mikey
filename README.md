# 🎙️Mikey, an audio loopback, virtual linux soundcard driver.

> [!WARNING] 
> This is a work-in-progress project.

Inspired by [v4l2loopback](https://github.com/umlaeute/v4l2loopback), Mikey functions as a loopback device that is capable of receiving audio playback and outputting it to other applications as if it were a microphone. It can be used to test userspace audio programs as well, which is a common use of virtual drivers.  

In-tree kernel module `snd_aloop` has richer functionality, but I think I can do it in a simpler way so that anyone can grab the code and modify it. Mikey is a work-in-progress project that takes some references from in-tree `snd_pcmtest` module and [vsnd](https://github.com/sysprog21/vsnd). Currently a playback device and a capture device can be listed from `aplay -l` and `arecord -l` after inserting the module.

## Testing
This kernel module is not battle-tested yet so I recommend using a virtual environment like [vritme-ng](https://github.com/arighi/virtme-ng) to test it until it is reliable enough. If `snd_pcm` module has not been loaded, use `modprobe snd_pcm` to load it first. Test Mikey with `aplay -D plughw:CARD=mikey,DEV=0 /path/to/audio/file`.

## ToDo
- [x] Use timer interrupt to simulate hardware interrupt for updating substream buffer (Currently it would hang the kernel)
- [ ] Loopback pcm data
- [ ] Make capture and playback device share the same buffer (SPSC queue) to reduce data copying
- [ ] Accept control options through `ioctl`

## Writing an ALSA soundcard driver
Since there is a lack of materials on this topic on the internet, here is my note on writing Mikey. Notice that it may not be as complete as a document like [the one in kernel](https://www.kernel.org/doc/html/latest/sound/kernel-api/writing-an-alsa-driver.html), but it should be more beginner friendly.  

(To be continued...)
