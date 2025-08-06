use ec_demo::app::App;

use color_eyre::Result;
use ratatui;

fn main() -> Result<()> {
    color_eyre::install()?;
    let terminal = ratatui::init();
    App::default().run(terminal)
}
