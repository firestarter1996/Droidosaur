package io.jor.nanosaur;

import android.content.Context;
import android.os.Build;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;

import org.libsdl.app.SDLActivity;

/**
 * Nanosaur Android Activity.
 * Extends SDLActivity to configure SDL3's Android integration.
 * SDL3 renames main() to SDL_main() via SDL_main.h macro, so
 * getMainFunction() must return "SDL_main" (not "main").
 */
public class NanosaurActivity extends SDLActivity {

    private static Vibrator sVibrator;

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL3",
            "main"
        };
    }

    @Override
    protected String getMainFunction() {
        return "SDL_main";
    }

    /**
     * Haptic tick for the touch controls (called from TouchControls.c over JNI, 2026-09-19).
     * SDL3's own haptic list only covers game-controller rumble motors, so the phone's vibrator
     * is driven directly. amplitude 1..255; ms is the pulse length.
     */
    public static void vibrate(int ms, int amplitude) {
        try {
            if (sVibrator == null) {
                Context ctx = SDLActivity.getContext();
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    VibratorManager vm = (VibratorManager) ctx.getSystemService(Context.VIBRATOR_MANAGER_SERVICE);
                    sVibrator = vm.getDefaultVibrator();
                } else {
                    sVibrator = (Vibrator) ctx.getSystemService(Context.VIBRATOR_SERVICE);
                }
            }
            if (sVibrator == null || !sVibrator.hasVibrator()) return;
            if (sVibrator.hasAmplitudeControl()) {
                sVibrator.vibrate(VibrationEffect.createOneShot(ms, Math.max(1, Math.min(255, amplitude))));
            } else {
                sVibrator.vibrate(VibrationEffect.createOneShot(ms, VibrationEffect.DEFAULT_AMPLITUDE));
            }
        } catch (Exception ignored) {
        }
    }
}
