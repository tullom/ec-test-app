#[cfg(not(feature = "mock"))]
mod acpi;
mod app;
mod battery;
mod rtc;
mod thermal;
mod ucsi;

use crate::app::App;

use color_eyre::Result;
use ratatui;

fn main() -> Result<()> {
    color_eyre::install()?;
    let terminal = ratatui::init();
    App::default().run(terminal)
}
