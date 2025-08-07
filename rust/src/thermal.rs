use crate::app::Module;
use crate::{Source, Threshold};

use ratatui::{
    buffer::Buffer,
    crossterm::event::{Event, KeyCode, KeyEventKind},
    layout::{Constraint, Direction, Layout, Rect},
    style::{Color, Style, Stylize, palette::tailwind},
    symbols,
    text::{Line, Span},
    widgets::{Axis, Block, Borders, Chart, Dataset, Gauge, GraphType, Padding, Paragraph, Widget},
};
use std::collections::VecDeque;
use tui_input::{Input, backend::crossterm::EventHandler};

const LABEL_COLOR: Color = tailwind::SLATE.c200;
const MAX_SAMPLES: usize = 60;

// Split an area in a direction with given percentages
fn area_split(area: Rect, direction: Direction, first: u16, second: u16) -> [Rect; 2] {
    Layout::default()
        .direction(direction)
        .constraints([Constraint::Percentage(first), Constraint::Percentage(second)])
        .split(area)
        .as_ref()
        .try_into()
        .unwrap()
}

// Create a wrapping title block
fn title_block(title: &str, padding: u16) -> Block<'_> {
    let title = Line::from(title);
    Block::new()
        .borders(Borders::ALL)
        .padding(Padding::vertical(padding))
        .title(title)
        .fg(LABEL_COLOR)
}

#[cfg(not(feature = "mock"))]
fn data_source() -> Box<dyn Source> {
    Box::new(crate::acpi::Acpi::default())
}

#[cfg(feature = "mock")]
fn data_source() -> Box<dyn Source> {
    Box::new(crate::mock::Mock::default())
}

fn get_sensor_tmp() -> f64 {
    data_source().get_temperature().expect("Unable to get temperature")
}

// Always return mock data for thresholds until sensor GET/SET VAR and GET/SET THRS supported
fn get_sensor_thresholds() -> SensorThresholds {
    SensorThresholds {
        _warn_low: 13.0,
        warn_high: 35.0,
        prochot: 40.0,
        critical: 45.0,
    }
}

fn get_fan_rpm() -> f64 {
    data_source().get_rpm().expect("Unable to get fan RPM")
}

fn set_fan_rpm(rpm: f64) {
    data_source().set_rpm(rpm).expect("Unable to set fan RPM")
}

fn get_fan_bounds() -> FanRpmBounds {
    let min = data_source().get_min_rpm().expect("Unable to get min RPM");
    let max = data_source().get_max_rpm().expect("Unable to get min RPM");

    FanRpmBounds { min, max }
}

fn get_fan_levels() -> FanStateLevels {
    let on = data_source()
        .get_threshold(Threshold::On)
        .expect("Unable to get ON threshold");
    let ramping = data_source()
        .get_threshold(Threshold::Ramping)
        .expect("Unable to get RAMPING threshold");
    let max = data_source()
        .get_threshold(Threshold::Max)
        .expect("Unable to get MAX threshold");

    FanStateLevels { on, ramping, max }
}

// Properties for rendering a graph
struct Graph {
    title: &'static str,
    color: Color,
    samples: Vec<(f64, f64)>,

    x_axis: &'static str,
    x_bounds: [f64; 2],
    x_labels: [Span<'static>; 3],

    y_axis: &'static str,
    y_bounds: [f64; 2],
    y_labels: [Span<'static>; 3],
}

#[derive(Default)]
struct SampleBuf<T, const N: usize> {
    samples: VecDeque<T>,
}

impl<T: Into<f64> + Copy, const N: usize> SampleBuf<T, N> {
    // Insert a sample into the buffer and evict the oldest if full
    fn insert(&mut self, sample: T) {
        self.samples.push_back(sample);
        if self.samples.len() > N {
            self.samples.pop_front();
        }
    }

    // Converts the buffer into a format that ratatui can use
    // Probably more efficent way than copying but buffer is small and only called once a second
    fn get(&self) -> Vec<(f64, f64)> {
        self.samples
            .iter()
            .enumerate()
            .map(|(i, &val)| (i as f64, val.into()))
            .collect()
    }
}

#[derive(Default)]
struct SensorThresholds {
    _warn_low: f64,
    warn_high: f64,
    prochot: f64,
    critical: f64,
}

#[derive(Default)]
struct SensorState {
    skin_temp: f64,
    thresholds: SensorThresholds,
    samples: SampleBuf<f64, MAX_SAMPLES>,
}

impl SensorState {
    fn update(&mut self) {
        self.skin_temp = get_sensor_tmp();
        self.thresholds = get_sensor_thresholds();
        self.samples.insert(self.skin_temp);
    }
}

#[derive(Default)]
struct FanRpmBounds {
    min: f64,
    max: f64,
}

#[derive(Default)]
struct FanStateLevels {
    on: f64,
    ramping: f64,
    max: f64,
}

#[derive(Default)]
struct FanState {
    rpm: f64,
    rpm_bounds: FanRpmBounds,
    state_levels: FanStateLevels,
    samples: SampleBuf<u32, MAX_SAMPLES>,
}

impl FanState {
    fn update(&mut self) {
        self.rpm = get_fan_rpm();
        self.rpm_bounds = get_fan_bounds();
        self.state_levels = get_fan_levels();
        self.samples.insert(self.rpm as u32);
    }
}

#[derive(Default)]
pub struct Thermal {
    rpm_input: Input,
    sensor: SensorState,
    fan: FanState,
    t: usize,
}

impl Module for Thermal {
    fn title(&self) -> &'static str {
        "Thermal Information"
    }

    fn update(&mut self) {
        self.sensor.update();
        self.fan.update();
        self.t += 1;
    }

    fn render(&self, area: Rect, buf: &mut Buffer) {
        let [sensor_area, fan_area] = area_split(area, Direction::Horizontal, 50, 50);
        self.render_sensor(sensor_area, buf);
        self.render_fan(fan_area, buf);
    }

    fn handle_event(&mut self, evt: &Event) {
        if let Event::Key(key) = evt
            && key.code == KeyCode::Enter
            && key.kind == KeyEventKind::Press
        {
            if let Ok(rpm) = self.rpm_input.value_and_reset().parse() {
                set_fan_rpm(rpm);
            }
        } else {
            let _ = self.rpm_input.handle_event(evt);
        }
    }
}

impl Thermal {
    pub fn new() -> Self {
        let mut inst = Self::default();
        inst.update();
        inst
    }

    fn render_sensor(&self, area: Rect, buf: &mut Buffer) {
        let [chart_area, widget_area] = area_split(area, Direction::Vertical, 70, 30);
        let [stats_area, thresholds_area] = area_split(widget_area, Direction::Horizontal, 50, 50);
        self.render_sensor_chart(chart_area, buf);
        self.render_sensor_stats(stats_area, buf);
        self.render_sensor_thresholds(thresholds_area, buf);
    }

    fn render_sensor_chart(&self, area: Rect, buf: &mut Buffer) {
        let y_labels = [
            "0.0".bold(),
            Span::styled(
                format!("{:.1}", (self.sensor.thresholds.critical + 5.0) / 2.0),
                Style::default().bold(),
            ),
            Span::styled(
                format!("{:.1}", self.sensor.thresholds.critical + 5.0),
                Style::default().bold(),
            ),
        ];
        let graph = Graph {
            title: "Temperature vs Time",
            color: Color::Red,
            samples: self.sensor.samples.get(),
            x_axis: "Time (s)",
            x_bounds: [0.0, 60.0],
            x_labels: self.time_labels(),
            y_axis: "Temperature (°C)",
            y_bounds: [0.0, self.sensor.thresholds.critical + 5.0],
            y_labels,
        };
        self.render_chart(area, buf, graph);
    }

    fn create_sensor_stats(&self) -> Vec<Line<'static>> {
        vec![Line::raw(format!("Skin temp: {:.2} °C", self.sensor.skin_temp))]
    }

    fn render_sensor_stats(&self, area: Rect, buf: &mut Buffer) {
        let stats_title = title_block("Live Temperature", 1);
        let inner = stats_title.inner(area);
        stats_title.render(area, buf);
        let [temp_area, gauge_area] = area_split(inner, Direction::Vertical, 50, 50);

        let gauge_color = if self.sensor.skin_temp < self.sensor.thresholds.warn_high {
            tailwind::GREEN.c700
        } else if self.sensor.skin_temp < self.sensor.thresholds.prochot {
            tailwind::YELLOW.c700
        } else if self.sensor.skin_temp < self.sensor.thresholds.critical {
            tailwind::ORANGE.c700
        } else {
            tailwind::RED.c700
        };
        let gauge_percent = (((self.sensor.skin_temp / self.sensor.thresholds.critical) * 100.0) as u16).clamp(0, 100);
        Paragraph::new(self.create_sensor_stats()).render(temp_area, buf);
        Gauge::default()
            .gauge_style(gauge_color)
            .percent(gauge_percent)
            .render(gauge_area, buf);
    }

    fn create_sensor_thresholds(&self) -> Vec<Line<'static>> {
        vec![
            Line::raw(format!("Warn:     {} °C", self.sensor.thresholds.warn_high.round())),
            Line::raw(format!("Prochot:  {} °C", self.sensor.thresholds.prochot.round())),
            Line::raw(format!("Critical: {} °C", self.sensor.thresholds.critical.round())),
        ]
    }

    fn render_sensor_thresholds(&self, area: Rect, buf: &mut Buffer) {
        let title = title_block("Thresholds", 1);
        Paragraph::new(self.create_sensor_thresholds())
            .block(title)
            .render(area, buf);
    }

    fn render_fan(&self, area: Rect, buf: &mut Buffer) {
        let [chart_area, widget_area] = area_split(area, Direction::Vertical, 70, 30);
        let [stats_area, levels_area] = area_split(widget_area, Direction::Horizontal, 50, 50);
        self.render_fan_chart(chart_area, buf);
        self.render_fan_stats(stats_area, buf);
        self.render_fan_levels(levels_area, buf);
    }

    fn render_fan_chart(&self, area: Rect, buf: &mut Buffer) {
        let y_labels = [
            "0.0".bold(),
            Span::styled((self.fan.rpm_bounds.max / 2.0).to_string(), Style::default().bold()),
            Span::styled(self.fan.rpm_bounds.max.to_string(), Style::default().bold()),
        ];
        let graph = Graph {
            title: "Fan RPM vs Time",
            color: Color::Blue,
            samples: self.fan.samples.get(),
            x_axis: "Time (s)",
            x_bounds: [0.0, 60.0],
            x_labels: self.time_labels(),
            y_axis: "RPM",
            y_bounds: [0.0, self.fan.rpm_bounds.max],
            y_labels,
        };
        self.render_chart(area, buf, graph);
    }

    fn create_fan_stats(&self) -> Vec<Line<'static>> {
        vec![Line::raw(format!(
            "RPM: {} ({}, {})",
            self.fan.rpm.round(),
            self.fan.rpm_bounds.min,
            self.fan.rpm_bounds.max
        ))]
    }

    fn render_fan_stats(&self, area: Rect, buf: &mut Buffer) {
        let title = title_block("Live Fan RPM", 0);
        let inner = title.inner(area);
        title.render(area, buf);

        let [rpm_area, input_area] = area_split(inner, Direction::Vertical, 30, 70);

        Paragraph::new(self.create_fan_stats()).render(rpm_area, buf);
        self.render_fan_rpm_input(input_area, buf);
    }

    fn create_fan_levels(&self) -> Vec<Line<'static>> {
        vec![
            Line::raw(format!("On:      {} °C", self.fan.state_levels.on.round())),
            Line::raw(format!("Ramping: {} °C", self.fan.state_levels.ramping.round())),
            Line::raw(format!("Max:     {} °C", self.fan.state_levels.max.round())),
        ]
    }

    fn render_fan_levels(&self, area: Rect, buf: &mut Buffer) {
        let title = title_block("Fan State Levels", 1);
        Paragraph::new(self.create_fan_levels()).block(title).render(area, buf);
    }

    fn render_fan_rpm_input(&self, area: Rect, buf: &mut Buffer) {
        let width = area.width.max(3) - 3;
        let scroll = self.rpm_input.visual_scroll(width as usize);

        let input = Paragraph::new(self.rpm_input.value())
            .style(Style::default())
            .scroll((0, scroll as u16))
            .block(Block::bordered().title("Set Fan RPM <ENTER>"));
        input.render(area, buf);
    }

    fn time_labels(&self) -> [Span<'static>; 3] {
        if self.t <= MAX_SAMPLES {
            ["0".bold(), "30".bold(), "60".bold()]
        } else {
            let a = (self.t - 60).to_string();
            let b = (self.t - 30).to_string();
            let c = self.t.to_string();
            [
                Span::styled(a, Style::default().bold()),
                Span::styled(b, Style::default().bold()),
                Span::styled(c, Style::default().bold()),
            ]
        }
    }

    fn render_chart(&self, area: Rect, buf: &mut Buffer, graph: Graph) {
        let samples = &graph.samples[..];
        let datasets = vec![
            Dataset::default()
                .marker(symbols::Marker::Braille)
                .style(Style::default().fg(graph.color))
                .graph_type(GraphType::Line)
                .data(samples),
        ];

        let chart = Chart::new(datasets)
            .block(Block::bordered().title(Line::from(graph.title).cyan().bold().centered()))
            .x_axis(
                Axis::default()
                    .title(graph.x_axis)
                    .style(Style::default().gray())
                    .bounds(graph.x_bounds)
                    .labels(graph.x_labels),
            )
            .y_axis(
                Axis::default()
                    .title(graph.y_axis)
                    .style(Style::default().gray())
                    .bounds(graph.y_bounds)
                    .labels(graph.y_labels),
            );

        chart.render(area, buf);
    }
}
