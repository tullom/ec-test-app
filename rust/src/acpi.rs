// This module maps the data returned from call into the C-Library to RUST structures
unsafe extern "C" {
    fn EvaluateAcpi(eval: *const i8, eval_len: usize, buffer: *mut u8, buf_len: &mut usize) -> i32;
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

impl std::fmt::Display for AcpiParseError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self)
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

// Implement conversion from Vec[u8] to AcpiEvalOutputBufferV1

impl Acpi {
    pub fn evaluate(eval: &str) -> Result<AcpiEvalOutputBufferV1, AcpiParseError> {
        let eval_ptr = eval.as_ptr(); // Transfers ownership
        let eval_len = eval.bytes().len();

        // Output buffer
        let mut buf_len = 1024;
        let mut buffer = vec![0u8; buf_len];

        let _res = unsafe { EvaluateAcpi(eval_ptr as *const i8, eval_len, buffer.as_mut_ptr(), &mut buf_len) };

        AcpiEvalOutputBufferV1::try_from(buffer)
    }
}
