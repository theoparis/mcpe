use std::io::BufReader;
use std::path::{Path, PathBuf};

/// Load a PNG file and return its RGBA pixel data, width, and height.
pub fn load_png(path: &Path) -> Option<(Vec<u8>, u32, u32)> {
    let file = std::fs::File::open(path).ok()?;
    let reader = BufReader::new(file);
    let decoder = png::Decoder::new(reader);
    let mut reader = decoder.read_info().ok()?;
    let mut buf = vec![0u8; reader.output_buffer_size()?];
    let info = reader.next_frame(&mut buf).ok()?;

    let bytes = &buf[..info.buffer_size()];

    // Convert to RGBA if needed
    let rgba = match info.color_type {
        png::ColorType::Rgba => bytes.to_vec(),
        png::ColorType::Rgb => {
            let mut rgba = Vec::with_capacity((info.width * info.height * 4) as usize);
            for pixel in bytes.chunks(3) {
                rgba.extend_from_slice(pixel);
                rgba.push(255);
            }
            rgba
        }
        png::ColorType::GrayscaleAlpha => {
            let mut rgba = Vec::with_capacity((info.width * info.height * 4) as usize);
            for pixel in bytes.chunks(2) {
                rgba.push(pixel[0]);
                rgba.push(pixel[0]);
                rgba.push(pixel[0]);
                rgba.push(pixel[1]);
            }
            rgba
        }
        png::ColorType::Grayscale => {
            let mut rgba = Vec::with_capacity((info.width * info.height * 4) as usize);
            for &pixel in bytes {
                rgba.push(pixel);
                rgba.push(pixel);
                rgba.push(pixel);
                rgba.push(255);
            }
            rgba
        }
        png::ColorType::Indexed => {
            log::warn!("Indexed color PNG not supported, skipping");
            return None;
        }
    };

    Some((rgba, info.width, info.height))
}

/// Search for data files in standard locations, matching the C++ `getDataSearchPaths()`.
pub fn find_data_dir() -> PathBuf {
    let candidates = [
        // Relative to CWD
        PathBuf::from("data"),
        PathBuf::from("../data"),
        PathBuf::from("../../data"),
    ];

    // Also check MCPE_DATA_DIR env var
    if let Ok(env_dir) = std::env::var("MCPE_DATA_DIR") {
        let p = PathBuf::from(env_dir);
        if p.exists() {
            return p;
        }
    }

    for candidate in &candidates {
        if candidate.exists() && candidate.is_dir() {
            return candidate.clone();
        }
    }

    // Last resort
    PathBuf::from("data")
}
