use ratatui::{
    buffer::Buffer,
    layout::Rect,
    style::{Color, Stylize, palette::tailwind},
    text::Line,
    widgets::{Block, Borders, Padding, Paragraph, Widget},
};

const LABEL_COLOR: Color = tailwind::SLATE.c200;

#[derive(Default)]
pub struct Rtc {}

impl Rtc {
    pub fn render(area: Rect, buf: &mut Buffer) {
        let status_title = title_block("RTC Properties");
        Paragraph::default().block(status_title).render(area, buf);
    }
}

fn title_block(title: &str) -> Block {
    let title = Line::from(title);
    Block::new()
        .borders(Borders::NONE)
        .padding(Padding::vertical(1))
        .title(title)
        .fg(LABEL_COLOR)
}
