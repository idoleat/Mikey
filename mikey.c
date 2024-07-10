#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION(
    "Mikey, an audio loopback device, also a virtual sound card.");
MODULE_AUTHOR("idoleat");

#define N_SUBSTREAM 1
#define N_PLAYBACK 1
#define N_CAPTURE 1

static int index[SNDRV_CARDS] =
    SNDRV_DEFAULT_IDX; /* Index [0...(SNDRV_CARDS - 1)] */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR; /* ID for this card */

/* platform device actions */
static void pdev_release(struct device *);

/* platform driver actions */
static int mikey_probe_pdev(struct platform_device *);
static void mikey_remove_pdev(struct platform_device *);

/* callbacks */
static int mikey_playback_open(struct snd_pcm_substream *substream);
static int mikey_playback_close(struct snd_pcm_substream *substream);
static int mikey_capture_open(struct snd_pcm_substream *substream);
static int mikey_capture_close(struct snd_pcm_substream *substream);
static int mikey_pcm_hw_params(struct snd_pcm_substream *substream,
                               struct snd_pcm_hw_params *hw_params);
static int mikey_pcm_hw_free(struct snd_pcm_substream *substream);
static int mikey_pcm_prepare(struct snd_pcm_substream *substream);
static int mikey_pcm_trigger(struct snd_pcm_substream *substream, int cmd);
static snd_pcm_uframes_t mikey_pcm_pointer(struct snd_pcm_substream *substream);
static void mikey_timer_callback(struct timer_list *t);

/* new pcm device */
static int mikey_new_pcm(struct snd_card *card);

struct mikey {
    struct snd_pcm *pcm;
    struct snd_card *card;
    struct platform_device *pdev;
    struct timer_list timer;
    unsigned int period_size;
    unsigned int buffer_size;
    unsigned int position;
};

static struct platform_device mikey_pdev = {.name = "Mikey",
                                            .dev = {
                                                .release = pdev_release,
                                            }};

static struct platform_driver mikey_pdrv = {
    .probe = mikey_probe_pdev,
    .remove_new = mikey_remove_pdev,
    .driver =
        {
            .name = "Mikey",
        },
};

static struct snd_pcm_ops mikey_playback_ops = {
    .open = mikey_playback_open,
    .close = mikey_playback_close,
    .ioctl = NULL,
    .hw_params = mikey_pcm_hw_params,
    .hw_free = mikey_pcm_hw_free,
    .prepare = mikey_pcm_prepare,
    .trigger = mikey_pcm_trigger,
    .sync_stop = NULL,
    .pointer = mikey_pcm_pointer,
    /* .mmap */
};

static struct snd_pcm_ops mikey_capture_ops = {
    .open = mikey_capture_open,
    .close = mikey_capture_close,
    .ioctl = NULL,
    .hw_params = mikey_pcm_hw_params,
    .hw_free = mikey_pcm_hw_free,
    .prepare = mikey_pcm_prepare,
    .trigger = mikey_pcm_trigger,
    .sync_stop = NULL,
    .pointer = mikey_pcm_pointer,
    /* .mmap */
};

/* playback device (speaker/headphone) */
static struct snd_pcm_hardware mikey_playback_hw = {
    .info = (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
             SNDRV_PCM_INFO_BLOCK_TRANSFER | SNDRV_PCM_INFO_MMAP_VALID),
    .formats = SNDRV_PCM_FMTBIT_S16_LE,
    .rates = SNDRV_PCM_RATE_8000_48000,
    .rate_min = 8000,
    .rate_max = 48000,
    .channels_min = 2,
    .channels_max = 2,
    .buffer_bytes_max = 32768,
    .period_bytes_min = 4096,
    .period_bytes_max = 32768,
    .periods_min = 1,
    .periods_max = 1024,
};

/* capture device (microphone) */
static struct snd_pcm_hardware mikey_capture_hw = {
    .info = (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
             SNDRV_PCM_INFO_BLOCK_TRANSFER | SNDRV_PCM_INFO_MMAP_VALID),
    .formats = SNDRV_PCM_FMTBIT_S16_LE,
    .rates = SNDRV_PCM_RATE_8000_48000,
    .rate_min = 8000,
    .rate_max = 48000,
    .channels_min = 2,
    .channels_max = 2,
    .buffer_bytes_max = 32768,
    .period_bytes_min = 4096,
    .period_bytes_max = 32768,
    .periods_min = 1,
    .periods_max = 1024,
};

static void pdev_release(struct device * dev)
{
    return;
}


static void mikey_remove_pdev(struct platform_device * pdev)
{
    return;
}

/* Create new sound card and its pcm component, but actually pcm contains a
 * pointer to card.
 */
static int mikey_probe_pdev(struct platform_device *pdev)
{
    struct mikey *mike;
    struct snd_card *card;
    int err;

    /* dma_set_mask_and_coherent ? */
    /* err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
    if (err)
        return err; */

    /* Allocate struct mikey with card for potentially more cards
     * with a Mikey on each, so you could have multiple pairs of
     * loopback. Note that Mikey points back to the one and the only
     * platform device.
     */
    err = snd_devm_card_new(&pdev->dev, index[pdev->id], id[pdev->id],
                            THIS_MODULE, sizeof(struct mikey), &card);
    if (err < 0) {
        pr_err("Failed on creating a new sound card.");
        return err;
    }

    mike = card->private_data; /* private_free? */
    mike->card = card;
    mike->pdev = pdev;
    strcpy(card->driver, "Mikey Driver");
    strcpy(card->shortname, "mikey");
    strcpy(card->longname, "Mikey Virtual Driver");

    timer_setup(&mike->timer, mikey_timer_callback, 0);
    mike->position = 0;

    err = mikey_new_pcm(card);
    if (err < 0)
        goto _err_free_card;

    err = snd_card_register(card);
    if (err < 0)
        goto _err_free_card;

    platform_set_drvdata(pdev, card);

    return 0;

_err_free_card:
    /* TODO: sound/drivers/pcmtest.c should have this */
    pr_err("Failed on probing");
    err = snd_card_free_on_error(&pdev->dev, err);
    if (err < 0)
        pr_err("Failed on probing but also failed on free card.");

    return err;
}

/* new pcm device */
static int mikey_new_pcm(struct snd_card *card)
{
    struct snd_pcm *pcm;
    int err;

    /* create a PCM called PCM0 with index 0, 1 play sub, 1 cap sub */
    /* If a chip supports multiple playbacks or captures, you can specify more
     * numbers, but they must be handled properly in open/close, etc.
     * callbacks.*/
    err = snd_pcm_new(card, "PCM0", 0, N_PLAYBACK, N_CAPTURE, &pcm);
    if (err < 0)
        return err;

    ((struct mikey *) card->private_data)->pcm = pcm;
    pcm->private_data = card->private_data;
    strcpy(pcm->name, "loopback pair");
    /* Not sure why pdev or pdrv can not store this */

    snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &mikey_playback_ops);
    snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &mikey_capture_ops);

    /* This creates a proc file for each substream */
    /* vmalloc'ed intermediate buffer. zero is passed as both the size and the
     * max size argument here. Since each vmalloc call should succeed at any
     * time, we don’t need to pre-allocate the buffers like other continuous
     * pages. */
    err =
        snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_VMALLOC, NULL, 0, 0);

    return 0;
}



/* playback open callback */
static int mikey_playback_open(struct snd_pcm_substream *substream)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    runtime->hw = mikey_playback_hw;

    return 0;
}

/* capture open callback */
static int mikey_capture_open(struct snd_pcm_substream *substream)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    runtime->hw = mikey_capture_hw;

    return 0;
}

/* playback close callback */
static int mikey_playback_close(struct snd_pcm_substream *substream)
{
    return 0;
}

/* capture close callback */
static int mikey_capture_close(struct snd_pcm_substream *substream)
{
    return 0;
}

/* hw_params callback */
static int mikey_pcm_hw_params(struct snd_pcm_substream *substream,
                               struct snd_pcm_hw_params *hw_params)
{
    /* This is called when the hardware parameters (hw_params) are set up by the
    application, that is, once when the buffer size, the period size, the
    format, etc. are defined for the PCM substream.

    Many hardware setups should be done in this callback, including the
    allocation of buffers.

    Parameters to be initialized are retrieved by the params_xxx() macros. */
    return 0;
}

/* hw_free callback */
static int mikey_pcm_hw_free(struct snd_pcm_substream *substream)
{
    return 0;
}

/* prepare callback */
static int mikey_pcm_prepare(struct snd_pcm_substream *substream)
{
    /* The difference from hw_params is that the prepare callback will be called
     * each time snd_pcm_prepare() is called, i.e. when recovering after
     * underruns, etc. */
    struct mikey *mike = substream->private_data;
    struct snd_pcm_runtime *runtime = substream->runtime;
    mike->position = 0;
    mike->period_size = frames_to_bytes(runtime, runtime->period_size);
    mike->buffer_size = frames_to_bytes(runtime, runtime->buffer_size);
    return 0;
}

/* trigger callback */
static int mikey_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
    /* This is called when the PCM is started, stopped or paused. */
    struct mikey *mike = substream->private_data;
    switch (cmd) {
    case SNDRV_PCM_TRIGGER_START:
        /* Start the timer to simulate the hardware interrupt */
        mod_timer(&mike->timer, jiffies + msecs_to_jiffies(10));
        break;
    case SNDRV_PCM_TRIGGER_STOP:
        /* Stop the timer */
        del_timer_sync(&mike->timer);
        break;
    default:
        return -EINVAL;
    }
    return 0;
}

/* pointer callback */
static snd_pcm_uframes_t mikey_pcm_pointer(struct snd_pcm_substream *substream)
{
    /* This callback is called when the PCM middle layer inquires the current
    hardware position in the buffer. The position must be returned in frames,
    ranging from 0 to buffer_size - 1.

    This is usually called from the buffer-update routine in the PCM middle
    layer, which is invoked when snd_pcm_period_elapsed() is called by the
    interrupt routine. Then the PCM middle layer updates the position and
    calculates the available space, and wakes up the sleeping poll threads, etc.

    This callback is also atomic by default. */
    struct mikey *mike = substream->private_data;
    return bytes_to_frames(substream->runtime, mike->position);
}

/* timer callback */
static void mikey_timer_callback(struct timer_list *t)
{
    struct mikey *mike = from_timer(mike, t, timer);
    struct snd_pcm_substream *substream = mike->pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;

    mike->position += mike->period_size;
    if (mike->position >= mike->buffer_size)
        mike->position -= mike->buffer_size;

    snd_pcm_period_elapsed(substream);

    /* Restart the timer */
    mod_timer(&mike->timer, jiffies + msecs_to_jiffies(10));
}

static int __init mod_init(void)
{
    int err;
    err = platform_device_register(&mikey_pdev);
    if (err) {
        platform_device_put(&mikey_pdev); /* pcmtest.c didn't do this */
        return err;
    }

    /* not to confuse with platform_register_drivers!! */
    err = platform_driver_register(&mikey_pdrv);
    if (err)
        platform_device_unregister(&mikey_pdev);

    pr_info("Hi I am Mikey! Feed me amazing sound!\n");
    return err;
}

static void __exit mod_exit(void)
{
    pr_info("Ok. Mikey will shutup now.");
    /* unregister in reverse register order? */
    platform_driver_unregister(&mikey_pdrv);
    platform_device_unregister(&mikey_pdev);
}

module_init(mod_init);
module_exit(mod_exit);
