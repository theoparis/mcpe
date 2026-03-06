use std::time::Instant;

/// Fixed-tick timer matching the original C++ `Timer` class.
///
/// Runs at 20 ticks per second with interpolation factor `alpha` for smooth rendering.
pub struct Timer {
    pub ticks_per_second: f32,
    /// Number of ticks to process this frame.
    pub ticks: i32,
    /// Interpolation factor in `[0, 1)` for rendering between ticks.
    pub alpha: f32,
    pub time_scale: f32,

    passed_time: f32,
    last_instant: Instant,
    adjust_time: f32,
}

const MAX_TICKS_PER_UPDATE: i32 = 10;

impl Timer {
    pub fn new(ticks_per_second: f32) -> Self {
        let now = Instant::now();
        Self {
            ticks_per_second,
            ticks: 0,
            alpha: 0.0,
            time_scale: 1.0,
            passed_time: 0.0,
            last_instant: now,
            adjust_time: 1.0,
        }
    }

    /// Advance the timer based on elapsed real time.
    /// After calling, `self.ticks` contains the number of game ticks to run,
    /// and `self.alpha` contains the interpolation factor for rendering.
    pub fn advance_time(&mut self) {
        let now = Instant::now();
        let elapsed = now.duration_since(self.last_instant).as_secs_f32();
        self.last_instant = now;

        let passed_seconds = (elapsed * self.adjust_time).clamp(0.0, 1.0);

        self.passed_time += passed_seconds * self.time_scale * self.ticks_per_second;
        self.ticks = self.passed_time as i32;
        self.passed_time -= self.ticks as f32;

        if self.ticks > MAX_TICKS_PER_UPDATE {
            self.ticks = MAX_TICKS_PER_UPDATE;
        }

        self.alpha = self.passed_time;
    }
}
