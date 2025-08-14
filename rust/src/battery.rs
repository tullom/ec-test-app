#[cfg(not(feature = "mock"))]
use crate::acpi::{Acpi, AcpiEvalOutputBufferV1};
use crate::app::Module;

use crossterm::event::Event;
use ratatui::prelude::Direction;
use ratatui::widgets::canvas::{self, Map, MapResolution};
use ratatui::{
    buffer::Buffer,
    layout::Rect,
    style::{Color, Style, Stylize, palette::tailwind},
    text::Line,
    widgets::{Bar, BarChart, BarGroup, Block, Borders, Gauge, Padding, Paragraph, Widget},
};

const BATGAUGE_COLOR_HIGH: Color = tailwind::GREEN.c500;
const BATGAUGE_COLOR_MEDIUM: Color = tailwind::YELLOW.c500;
const BATGAUGE_COLOR_LOW: Color = tailwind::RED.c500;

const LABEL_COLOR: Color = tailwind::SLATE.c200;

/// BST: ACPI Battery Status
#[derive(Default)]
pub struct BstData {
    state: u32,
    rate: u32,
    capacity: u32,
    voltage: u32,
}

/*
/// BIX: ACPI Battery Informatino eXtended
#[derive(Default)]
pub struct BixData {
    revision: u32,
    power_unit: u32,    // 0 - mW, 1 - mA
    design_capacity: u32,
    last_full_capacity: u32,
    battery_technology: u32, // 0 - primary, 1 - secondary
    design_voltage: u32,
    warning_capacity: u32,
    low_capacity: u32,
    cycle_count: u32,
    accuracy: u32,  // Thousands of a percent
    max_sample_time: u32,
    min_sample_time: u32,   // Milliseconds
    max_average_interval: u32,
    min_average_internal: u32,
    capacity_gran1: u32,
    capacity_gran2: u32,
    model_number: Vec<u8>,
    serial_number: Vec<u8>,
    battery_type: Vec<u8>,
    oem_info: Vec<u8>,
    swap_cap: u32,
}
*/

#[derive(Default)]
pub struct Battery {}

impl Module for Battery {
    fn title(&self) -> &'static str {
        "Battery + PSU Information"
    }

    fn update(&mut self) {}

    fn handle_event(&mut self, _evt: &Event) {}

    fn render(&self, area: Rect, buf: &mut Buffer) {
        let [text_area, img_area] = crate::thermal::area_split(area, Direction::Horizontal, 75, 25);
        let bat_status = Self::get_bst();
        //let bat_info = Self::get_bix();
        //let bat_percent = (bat_status.capacity / bat_info.design_capacity) * 100;
        let bat_percent = bat_status.capacity / 82; // Use fake values till BIX is available

        let status_title = Self::title_block("Battery Status");
        Paragraph::new(Self::create_status(area, bat_status))
            .block(status_title)
            .render(area, buf);

        /*
        let info_title = Self::title_block("Battery Info");
        let bix_area = Rect::new(area.x, area.y + 6, area.width / 2, 4);
        Paragraph::new(Self::create_info(bix_area,bat_info))
            .block(info_title)
            .render(bix_area, buf);
        */

        let gauge_area = Rect::new(area.x, area.y + 20, area.width / 2, 4);
        let title = Self::title_block("");
        Gauge::default()
            .block(title)
            .gauge_style(match bat_percent {
                0..=20 => BATGAUGE_COLOR_LOW,
                21..=60 => BATGAUGE_COLOR_MEDIUM,
                _ => BATGAUGE_COLOR_HIGH,
            })
            .percent(bat_percent.try_into().unwrap())
            .use_unicode(true)
            .render(gauge_area, buf);

        BarChart::default()
            .block(
                Block::bordered()
                    .title_alignment(ratatui::layout::Alignment::Center)
                    .title("BarChart"),
            )
            .bar_width(img_area.width - 2)
            .bar_gap(0)
            .group_gap(0)
            .bar_style(Style::new().green().on_white())
            .value_style(Style::new().red().bold())
            .label_style(Style::new().white())
            .data(&[("Battery level", 99)])
            .max(100)
            .render(img_area, buf);
        canvas::Canvas::default()
            // .block(Block::bordered().title("Canvas"))
            .marker(ratatui::symbols::Marker::Block)
            // .x_bounds([-180.0, 180.0])
            // .y_bounds([-90.0, 90.0])
            .paint(|ctx| {
                ctx.draw(&canvas::Rectangle {
                    x: img_area.x.into(),
                    y: img_area.y.into(),
                    width: (img_area.width - 2).into(),
                    height: (img_area.height - 2).into(),
                    color: Color::Red,
                });
            })
            .render(img_area, buf);
        let text = vec![
            Line::from(vec![
                ratatui::text::Span::raw("First"),
                ratatui::text::Span::styled("line", Style::new().green().italic()),
                ".".into(),
            ]),
            Line::from("Second line".red()),
            "Third line".into(),
        ];

        // Split vertically into 3 chunks: top, middle, bottom
        let vertical_chunks = ratatui::layout::Layout::default()
            .direction(Direction::Vertical)
            .constraints([
                ratatui::layout::Constraint::Percentage(40),
                ratatui::layout::Constraint::Percentage(20),
                ratatui::layout::Constraint::Percentage(40),
            ])
            .split(img_area);

        Paragraph::new(text)
            // .style(Style::new().white().on_black())
            .alignment(ratatui::layout::Alignment::Center)
            .wrap(ratatui::widgets::Wrap { trim: true })
            .render(vertical_chunks[1], buf);
    }
}

// Convert ACPI result to BstData
#[cfg(not(feature = "mock"))]
impl From<AcpiEvalOutputBufferV1> for BstData {
    fn from(data: AcpiEvalOutputBufferV1) -> Self {
        // We are expecting 4 32-bit values
        if data.count != 4 {
            panic!("Unexpected Count {}", data.count)
        }
        BstData {
            state: data.arguments[0].data_32,
            rate: data.arguments[1].data_32,
            capacity: data.arguments[2].data_32,
            voltage: data.arguments[3].data_32,
        }
    }
}

// Convert ACPI result to BixData
/*
impl From<AcpiEvalOutputBufferV1> for BixData {
    fn from(data: AcpiEvalOutputBufferV1) -> Self {
        // We are expecting 21 arguments
        if data.count != 21 {
            panic!("Unexpected Count {}", data.count)
        }
        BixData {
            revision: data.arguments[0].data_32,
            power_unit: data.arguments[1].data_32,
            design_capacity: data.arguments[2].data_32,
            last_full_capacity: data.arguments[3].data_32,
            battery_technology: data.arguments[4].data_32,
            design_voltage: data.arguments[5].data_32,
            warning_capacity: data.arguments[6].data_32,
            low_capacity: data.arguments[7].data_32,
            cycle_count: data.arguments[8].data_32,
            accuracy: data.arguments[9].data_32,
            max_sample_time: data.arguments[10].data_32,
            min_sample_time: data.arguments[11].data_32,
            max_average_interval: data.arguments[12].data_32,
            min_average_internal: data.arguments[13].data_32,
            capacity_gran1: data.arguments[14].data_32,
            capacity_gran2: data.arguments[15].data_32,
            model_number: data.arguments[16].data.clone(),
            serial_number: data.arguments[17].data.clone(),
            battery_type: data.arguments[18].data.clone(),
            oem_info: data.arguments[19].data.clone(),
            swap_cap: data.arguments[20].data_32,
        }
    }
}
*/

impl Battery {
    pub fn new() -> Self {
        Self {}
    }

    #[cfg(not(feature = "mock"))]
    fn get_bst() -> BstData {
        let result = Acpi::evaluate("\\_SB.ECT0.TBST", None);
        match result {
            Ok(value) => value.into(),
            Err(e) => panic!("Failed {e}"),
        }
    }

    #[cfg(feature = "mock")]
    fn get_bst() -> BstData {
        BstData {
            state: 2,
            rate: 3839,
            capacity: 5574,
            voltage: 12569,
        }
    }

    /*
    fn get_bix() -> BixData {
        let result = Acpi::evaluate("\\_SB.ECT0.TBIX");
        match result {
            Ok(value) => value.into(),
            Err(e) => panic!("Failed {}",e),
        }
    }
    */

    fn title_block(title: &str) -> Block<'_> {
        let title = Line::from(title);
        Block::new()
            .borders(Borders::NONE)
            .padding(Padding::vertical(1))
            .title(title)
            .fg(LABEL_COLOR)
    }

    fn create_status(_area: Rect, status: BstData) -> Vec<Line<'static>> {
        vec![
            Line::raw(format!("State:               {:?}", status.state)),
            Line::raw(format!("Present Rate:        {:?}mWh", status.rate)),
            Line::raw(format!("Remaining Capacity:  {:?}mWh", status.capacity)),
            Line::raw(format!("Present Voltage:     {:?}mV", status.voltage)),
        ]
    }

    /*
    fn create_info(_area: Rect, info: BixData) -> Vec<Line<'static>> {
        vec![
                Line::raw(format!("Design Capacity:     {:?}", info.design_capacity) ),
                Line::raw(format!("Cycle Count:         {:?}", info.cycle_count)),
                Line::raw(format!("Model Number:        {:?}", info.model_number)),
                Line::raw(format!("Serial Number:       {:?}", info.serial_number)),
            ]
    }
    */
}
