pub struct Random {
    mt: [u32; 624],
    mti: usize,
    have_next_next_gaussian: bool,
    next_next_gaussian: f32,
    seed: i64,
}

impl Random {
    const N: usize = 624;
    const M: usize = 397;
    const MATRIX_A: u32 = 0x9908b0df;
    const UPPER_MASK: u32 = 0x80000000;
    const LOWER_MASK: u32 = 0x7fffffff;

    pub fn new(seed: i64) -> Self {
        let mut r = Self {
            mt: [0; 624],
            mti: 625,
            have_next_next_gaussian: false,
            next_next_gaussian: 0.0,
            seed: 0,
        };
        r.set_seed(seed);
        r
    }

    pub fn set_seed(&mut self, seed: i64) {
        self.seed = seed;
        self.mti = Self::N + 1;
        self.have_next_next_gaussian = false;
        self.next_next_gaussian = 0.0;
        self.init_genrand(seed as u32);
    }

    fn init_genrand(&mut self, s: u32) {
        self.mt[0] = s;
        for i in 1..Self::N {
            self.mt[i] = 1812433253u32
                .wrapping_mul(self.mt[i - 1] ^ (self.mt[i - 1] >> 30))
                .wrapping_add(i as u32);
            self.mt[i] &= 0xffffffff;
        }
        self.mti = Self::N;
    }

    pub fn genrand_int32(&mut self) -> u32 {
        let mut y: u32;
        let mag01 = [0x0u32, Self::MATRIX_A];

        if self.mti >= Self::N {
            if self.mti == Self::N + 1 {
                self.init_genrand(5489);
            }

            for kk in 0..(Self::N - Self::M) {
                y = (self.mt[kk] & Self::UPPER_MASK) | (self.mt[kk + 1] & Self::LOWER_MASK);
                self.mt[kk] = self.mt[kk + Self::M] ^ (y >> 1) ^ mag01[(y & 0x1) as usize];
            }
            for kk in (Self::N - Self::M)..(Self::N - 1) {
                y = (self.mt[kk] & Self::UPPER_MASK) | (self.mt[kk + 1] & Self::LOWER_MASK);
                self.mt[kk] = self.mt[kk.wrapping_add(Self::M).wrapping_sub(Self::N)]
                    ^ (y >> 1)
                    ^ mag01[(y & 0x1) as usize];
            }
            y = (self.mt[Self::N - 1] & Self::UPPER_MASK) | (self.mt[0] & Self::LOWER_MASK);
            self.mt[Self::N - 1] = self.mt[Self::M - 1] ^ (y >> 1) ^ mag01[(y & 0x1) as usize];

            self.mti = 0;
        }

        y = self.mt[self.mti];
        self.mti += 1;

        y ^= y >> 11;
        y ^= (y << 7) & 0x9d2c5680;
        y ^= (y << 15) & 0xefc60000;
        y ^= y >> 18;

        y
    }

    pub fn next_int(&mut self) -> i32 {
        (self.genrand_int32() >> 1) as i32
    }

    pub fn next_int_n(&mut self, n: i32) -> i32 {
        if n <= 0 {
            return 0;
        }
        (self.genrand_int32() % n as u32) as i32
    }

    pub fn next_float(&mut self) -> f32 {
        self.genrand_int32() as f32 * (1.0 / 4294967296.0)
    }

    pub fn next_double(&mut self) -> f64 {
        self.genrand_int32() as f64 * (1.0 / 4294967296.0)
    }
}
