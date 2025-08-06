use crate::{Source, Threshold};
use color_eyre::Result;
use std::sync::{Mutex, OnceLock, atomic::AtomicI64};

static SET_RPM: AtomicI64 = AtomicI64::new(-1);
static SAMPLE: OnceLock<Mutex<(i64, i64)>> = OnceLock::new();

#[derive(Default)]
pub struct Mock {}

impl Mock {
    pub fn new() -> Self {
        Default::default()
    }
}

impl Source for Mock {
    fn get_temperature(&self) -> Result<f64> {
        let mut sample = SAMPLE.get_or_init(|| Mutex::new((2732, 1))).lock().unwrap();

        sample.0 += 10 * sample.1;
        if sample.0 >= 3232 || sample.0 <= 2732 {
            sample.1 *= -1;
        }

        Ok(dk_to_c(sample.0 as u32))
    }

    fn get_rpm(&self) -> Result<f64> {
        use std::f64::consts::PI;
        use std::sync::{Mutex, OnceLock};

        // For mock, if user sets RPM, we just always return what was last set instead of sin wave
        let set_rpm = SET_RPM.load(std::sync::atomic::Ordering::Relaxed);
        if set_rpm >= 0 {
            Ok(set_rpm as f64)
        } else {
            // Generate sin wave
            static SAMPLE: OnceLock<Mutex<f64>> = OnceLock::new();
            let mut sample = SAMPLE.get_or_init(|| Mutex::new(0.0)).lock().unwrap();

            let freq = 0.1;
            let amplitude = 3000.0;
            let base = 3000.0;
            let rpm = (sample.sin() * amplitude) + base;

            *sample += freq;
            if *sample > 2.0 * PI {
                *sample -= 2.0 * PI;
            }

            Ok(rpm)
        }
    }

    fn get_min_rpm(&self) -> Result<f64> {
        Ok(0.0)
    }

    fn get_max_rpm(&self) -> Result<f64> {
        Ok(6000.0)
    }

    fn get_threshold(&self, threshold: Threshold) -> Result<f64> {
        match threshold {
            Threshold::On => Ok(28.0),
            Threshold::Ramping => Ok(40.0),
            Threshold::Max => Ok(44.0),
        }
    }

    fn set_rpm(&self, rpm: f64) -> Result<()> {
        SET_RPM.store(rpm as i64, std::sync::atomic::Ordering::Relaxed);
        Ok(())
    }
}

// Convert deciKelvin to degrees Celsius
const fn dk_to_c(dk: u32) -> f64 {
    (dk as f64 / 10.0) - 273.15
}
