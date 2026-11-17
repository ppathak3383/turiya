// SPDX-License-Identifier: AGPL-3.0-or-later
//! The owner's file, handed to Sakshi (step 5e). Pure: no UEFI, no allocation, so it is tested on the host:
//!
//!     rustc --edition 2021 --test src/owner.rs -o /tmp/usas_owner && /tmp/usas_owner
//!
//! Sakshi's image carries one area for the file in its writable data (sakshi.cc, OWNER_FILE):
//!   bytes 0-15  the mark "TURIYA-OWNER-v1\0"
//!   byte 16     the state: 0 untouched, 1 read, 2 no file, 3 too large, 4 could not be read
//!   bytes 17-19 zero
//!   bytes 20-23 the length, little-endian (0 unless the state is 1; at most MAX)
//!   bytes 24-   the file, MAX bytes of room
//! Usas finds the area by its mark and writes the file into its staged copy of Sakshi before seL4 starts.
//! Only the writable data changes, so the build seal (which covers Sakshi's read-only segments) still holds.

pub const MARK: [u8; 16] = *b"TURIYA-OWNER-v1\0";
pub const MAX: usize = 4096;
pub const AREA: usize = 24 + MAX;

pub const READ: u8 = 1;
pub const NONE: u8 = 2;
pub const TOO_LARGE: u8 = 3;
pub const UNREADABLE: u8 = 4;

/// The offset of the area in Sakshi's image: the only copy of the mark, followed by AREA - 16 zero bytes
/// (an area never written before). Two marks, no mark, no room or a used area: None, and nothing is written.
pub fn find_area(img: &[u8]) -> Option<usize> {
    if img.len() < AREA { return None; }
    let mut at = None;
    for i in 0..=img.len() - MARK.len() {
        if img[i..i + MARK.len()] == MARK {
            if at.is_some() { return None; }
            at = Some(i);
        }
    }
    match at {
        Some(i) if i + AREA <= img.len() && img[i + MARK.len()..i + AREA].iter().all(|&b| b == 0) => Some(i),
        _ => None,
    }
}

/// Write the state, the length and the file into the area found by find_area. Only a READ state carries
/// bytes; a file over MAX bytes is refused (false, nothing written).
pub fn place(img: &mut [u8], at: usize, state: u8, file: &[u8]) -> bool {
    let n = if state == READ { file.len() } else { 0 };
    if n > MAX || state == 0 || state > UNREADABLE || at.checked_add(AREA).map_or(true, |e| e > img.len()) {
        return false;
    }
    img[at + 16] = state;
    img[at + 17..at + 20].fill(0);
    img[at + 20..at + 24].copy_from_slice(&(n as u32).to_le_bytes());
    img[at + 24..at + 24 + n].copy_from_slice(&file[..n]);
    true
}

#[cfg(test)]
mod tests {
    use super::*;

    fn image(before: usize, after: usize) -> Vec<u8> {
        let mut v = vec![0x5au8; before];
        v.extend_from_slice(&MARK);
        v.extend(std::iter::repeat(0u8).take(AREA - 16));
        v.extend(std::iter::repeat(0xa5u8).take(after));
        v
    }

    #[test]
    fn finds_the_one_area() {
        let img = image(1000, 77);
        assert_eq!(find_area(&img), Some(1000));
        assert_eq!(find_area(&image(0, 0)), Some(0));
    }

    #[test]
    fn refuses_two_marks_no_mark_no_room_or_a_used_area() {
        let mut two = image(100, 0);
        two.extend_from_slice(&MARK);
        two.extend(std::iter::repeat(0u8).take(AREA));
        assert_eq!(find_area(&two), None);
        assert_eq!(find_area(&vec![0u8; 3 * AREA]), None);
        let mut short = image(10, 0);
        short.truncate(10 + AREA - 1);
        assert_eq!(find_area(&short), None);
        let mut used = image(10, 0);
        used[10 + 16] = READ;
        assert_eq!(find_area(&used), None);
        used[10 + 16] = 0;
        used[10 + AREA - 1] = 1;
        assert_eq!(find_area(&used), None);
    }

    #[test]
    fn places_the_file_and_nothing_outside_the_area() {
        let mut img = image(500, 300);
        let at = find_area(&img).unwrap();
        let before = img.clone();
        let file = b"# known targets\nknown: papa\n";
        assert!(place(&mut img, at, READ, file));
        assert_eq!(img[at + 16], READ);
        assert_eq!(&img[at + 17..at + 20], &[0, 0, 0]);
        assert_eq!(u32::from_le_bytes([img[at + 20], img[at + 21], img[at + 22], img[at + 23]]) as usize, file.len());
        assert_eq!(&img[at + 24..at + 24 + file.len()], &file[..]);
        assert!(img[at + 24 + file.len()..at + AREA].iter().all(|&b| b == 0));
        assert_eq!(&img[..at + 16], &before[..at + 16]);
        assert_eq!(&img[at + AREA..], &before[at + AREA..]);
    }

    #[test]
    fn a_full_file_fits_and_one_byte_more_does_not() {
        let mut img = image(0, 0);
        let full = vec![b'x'; MAX];
        assert!(place(&mut img, 0, READ, &full));
        assert_eq!(&img[24..24 + MAX], &full[..]);
        let mut img2 = image(0, 0);
        let snapshot = img2.clone();
        assert!(!place(&mut img2, 0, READ, &vec![b'x'; MAX + 1]));
        assert_eq!(img2, snapshot);
    }

    #[test]
    fn other_states_carry_no_bytes() {
        for st in [NONE, TOO_LARGE, UNREADABLE] {
            let mut img = image(7, 7);
            assert!(place(&mut img, 7, st, b"known: papa\n"));
            assert_eq!(img[7 + 16], st);
            assert_eq!(&img[7 + 20..7 + 24], &[0, 0, 0, 0]);
            assert!(img[7 + 24..7 + AREA].iter().all(|&b| b == 0));
        }
    }

    #[test]
    fn refuses_a_bad_state_or_offset() {
        let mut img = image(0, 0);
        let snapshot = img.clone();
        assert!(!place(&mut img, 0, 0, b""));
        assert!(!place(&mut img, 0, 5, b""));
        assert!(!place(&mut img, 1, READ, b""));
        assert!(!place(&mut img, usize::MAX - 3, READ, b""));
        assert_eq!(img, snapshot);
    }
}
