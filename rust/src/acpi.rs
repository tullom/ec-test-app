use std::mem;

// This module maps the data returned from call into the C-Library to RUST structures
unsafe extern "C" {
    fn EvaluateAcpi(input: *const i8, input_len: usize, buffer: *mut u8, buf_len: &mut usize) -> i32;
}

#[repr(C)]
#[derive(Debug)]
pub struct AcpiEvalInputBufferComplexV1Ex {
    pub signature: u32,
    pub methodname: [u8; 256],
    pub size: u32,
    pub argumentcount: u32,
    pub arguments: AcpiMethodArgumentV1,
}

#[repr(C)]
#[derive(Debug, Default)]
pub struct AcpiEvalOutputBufferV1 {
    pub signature: u32,
    pub length: u32,
    pub count: u32,
    pub arguments: Vec<AcpiMethodArgumentV1>,
}

#[repr(C)]
#[derive(Debug, Default)]
pub struct AcpiMethodArgumentV1 {
    pub type_: u16,
    pub data_length: u16,
    pub data_32: u32,
    pub data: Vec<u8>,
}

#[derive(Debug)]
pub enum AcpiParseError {
    InsufficientLength,
    InvalidFormat,
}

pub const ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE_EX: u32 = u32::from_le_bytes(*b"AeiF");

impl std::fmt::Display for AcpiParseError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self)
    }
}

// Convert from &str into AcpiEvalInputBufferComplexV1Ex
impl TryFrom<&str> for AcpiEvalInputBufferComplexV1Ex {
    type Error = AcpiParseError;
    fn try_from(method: &str) -> Result<Self, AcpiParseError> {
        let mut buffer = [0u8; 256];
        let bytes = method.as_bytes();
        let len = bytes.len().min(256);
        buffer[..len].copy_from_slice(&bytes[..len]);
        Ok(AcpiEvalInputBufferComplexV1Ex {
            signature: ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE_EX,
            methodname: buffer,
            size: 0,          // Need to update with actual size based on parameters
            argumentcount: 0, // Update to allow multiple input arguments
            arguments: AcpiMethodArgumentV1::default(),
        })
    }
}

// Convert vec[u8] into AcpiEvalOutputBufferV1
impl TryFrom<Vec<u8>> for AcpiEvalOutputBufferV1 {
    type Error = AcpiParseError;
    fn try_from(value: Vec<u8>) -> Result<Self, AcpiParseError> {
        let signature = u32::from_le_bytes(value[0..4].try_into().map_err(|_| AcpiParseError::InvalidFormat)?);
        let length = u32::from_le_bytes(value[4..8].try_into().map_err(|_| AcpiParseError::InvalidFormat)?);
        let count = u32::from_le_bytes(value[8..12].try_into().map_err(|_| AcpiParseError::InvalidFormat)?);

        let mut offset = 12;
        let mut arguments = Vec::new();

        for _ in 0..count {
            if offset + 8 > value.len() {
                return Err(AcpiParseError::InsufficientLength);
            }

            let type_ = u16::from_le_bytes(value[offset..offset + 2].try_into().unwrap());
            let data_length = u16::from_le_bytes(value[offset + 2..offset + 4].try_into().unwrap()) as usize;
            let data_32 = if type_ == 0 {
                u32::from_le_bytes(value[offset + 4..offset + 8].try_into().unwrap())
            } else {
                0
            };
            offset += 4;

            if offset + data_length > value.len() {
                return Err(AcpiParseError::InsufficientLength);
            }

            let data = value[offset..offset + data_length].to_vec();
            offset += data_length;

            arguments.push(AcpiMethodArgumentV1 {
                type_,
                data_length: data_length as u16,
                data_32,
                data,
            });
        }

        // Now return generated content
        Ok(AcpiEvalOutputBufferV1 {
            signature,
            length,
            count,
            arguments,
        })
    }
}

pub struct Acpi {}

impl Acpi {
    pub fn evaluate(eval: &str) -> Result<AcpiEvalOutputBufferV1, AcpiParseError> {
        let input = AcpiEvalInputBufferComplexV1Ex::try_from(eval);

        // Output buffer
        let mut buf_len = 1024;
        let mut buffer = vec![0u8; buf_len];

        let _res = unsafe {
            EvaluateAcpi(
                &input as *const _ as *const i8,
                mem::size_of::<AcpiEvalInputBufferComplexV1Ex>(),
                buffer.as_mut_ptr(),
                &mut buf_len,
            )
        };

        AcpiEvalOutputBufferV1::try_from(buffer)
    }
}
