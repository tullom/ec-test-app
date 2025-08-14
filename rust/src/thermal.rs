use crate::app::Module;
use crate::{Source, Threshold};
use color_eyre::Result;

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
pub(crate) fn area_split(area: Rect, direction: Direction, first: u16, second: u16) -> [Rect; 2] {
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

// Combines a title string with a visual status indicator character
fn title_str_with_status(title: &str, success: bool) -> String {
    let status = if success { "✅" } else { "❌" };
    format!("{title} {status}")
}

fn get_sensor_tmp<S: Source>(source: &S) -> Result<f64> {
    source.get_temperature()
}

// Always return mock data for thresholds until sensor GET/SET VAR and GET/SET THRS supported
fn get_sensor_thresholds<S: Source>(_source: &S) -> Result<SensorThresholds> {
    Ok(SensorThresholds {
        _warn_low: 13.0,
        warn_high: 35.0,
        prochot: 40.0,
        critical: 45.0,
    })
}

fn get_fan_rpm<S: Source>(source: &S) -> Result<f64> {
    source.get_rpm()
}

fn set_fan_rpm<S: Source>(source: &S, rpm: f64) -> Result<()> {
    source.set_rpm(rpm)
}

fn get_fan_bounds<S: Source>(source: &S) -> Result<FanRpmBounds> {
    let min = source.get_min_rpm()?;
    let max = source.get_max_rpm()?;

    Ok(FanRpmBounds { min, max })
}

fn get_fan_levels<S: Source>(source: &S) -> Result<FanStateLevels> {
    let on = source.get_threshold(Threshold::On)?;
    let ramping = source.get_threshold(Threshold::Ramping)?;
    let max = source.get_threshold(Threshold::Max)?;

    Ok(FanStateLevels { on, ramping, max })
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
    temp_success: bool,
    thresholds: SensorThresholds,
    thresholds_success: bool,
    samples: SampleBuf<f64, MAX_SAMPLES>,
}

impl SensorState {
    fn update<S: Source>(&mut self, source: &S) {
        if let Ok(temp) = get_sensor_tmp(source) {
            self.skin_temp = temp;
            self.samples.insert(temp);
            self.temp_success = true;
        } else {
            self.temp_success = false;
        }

        if let Ok(thresholds) = get_sensor_thresholds(source) {
            self.thresholds = thresholds;
            self.thresholds_success = true;
        } else {
            self.thresholds_success = false;
        }
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
    rpm_success: bool,
    rpm_bounds: FanRpmBounds,
    bounds_success: bool,
    state_levels: FanStateLevels,
    levels_success: bool,
    samples: SampleBuf<u32, MAX_SAMPLES>,
}

impl FanState {
    fn update<S: Source>(&mut self, source: &S) {
        if let Ok(rpm) = get_fan_rpm(source) {
            self.rpm = rpm;
            self.samples.insert(rpm as u32);
            self.rpm_success = true;
        } else {
            self.rpm_success = false;
        }

        if let Ok(rpm_bounds) = get_fan_bounds(source) {
            self.rpm_bounds = rpm_bounds;
            self.bounds_success = true;
        } else {
            self.bounds_success = false;
        }

        if let Ok(state_levels) = get_fan_levels(source) {
            self.state_levels = state_levels;
            self.levels_success = true;
        } else {
            self.levels_success = false;
        }
    }
}

pub struct Thermal<S: Source> {
    rpm_input: Input,
    sensor: SensorState,
    fan: FanState,
    t: usize,
    source: S,
}

impl<S: Source> Module for Thermal<S> {
    fn title(&self) -> &'static str {
        "Thermal Information"
    }

    fn update(&mut self) {
        self.sensor.update(&self.source);
        self.fan.update(&self.source);
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
                let _ = set_fan_rpm(&self.source, rpm);
            }
        } else {
            let _ = self.rpm_input.handle_event(evt);
        }
    }
}

impl<S: Source> Thermal<S> {
    pub fn new(source: S) -> Self {
        let mut inst = Self {
            rpm_input: Default::default(),
            sensor: Default::default(),
            fan: Default::default(),
            t: Default::default(),
            source,
        };

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
        let title_str = title_str_with_status("Live Temperature", self.sensor.temp_success);
        let stats_title = title_block(&title_str, 1);
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
        let title_str = title_str_with_status("Thresholds", self.sensor.thresholds_success);
        let title = title_block(&title_str, 1);
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
        let title_str = title_str_with_status("Live Fan RPM", self.fan.rpm_success && self.fan.bounds_success);
        let title = title_block(&title_str, 0);
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
        let title_str = title_str_with_status("Fan State Levels", self.fan.levels_success);
        let title = title_block(&title_str, 1);
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
