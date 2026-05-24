use libc::{c_char, c_int, size_t};
use prop_rs_android::resetprop::ResetProp;
use prop_rs_android::sys_prop;
use std::ffi::{CStr, CString};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;
use std::sync::Once;

static INIT: Once = Once::new();
static mut INIT_OK: bool = false;

fn init_sys_prop_once() -> bool {
    INIT.call_once(|| {
        let ok = sys_prop::init().is_ok();
        unsafe {
            INIT_OK = ok;
        }
    });

    unsafe { INIT_OK }
}

fn resetprop_engine() -> ResetProp {
    ResetProp {
        skip_svc: false,
        persistent: false,
        persist_only: false,
        verbose: false,
        show_context: false,
    }
}

fn cstr_to_str<'a>(ptr: *const c_char) -> Option<&'a str> {
    if ptr.is_null() {
        return None;
    }

    unsafe { CStr::from_ptr(ptr).to_str().ok() }
}

fn ffi_guard<F>(f: F) -> bool
where
    F: FnOnce() -> bool,
{
    catch_unwind(AssertUnwindSafe(f)).unwrap_or(false)
}

#[no_mangle]
pub extern "C" fn dcfg_rp_set(key: *const c_char, value: *const c_char) -> bool {
    ffi_guard(|| {
        if !init_sys_prop_once() {
            return false;
        }

        let Some(key) = cstr_to_str(key) else {
            return false;
        };

        let Some(value) = cstr_to_str(value) else {
            return false;
        };

        resetprop_engine().set(key, value).is_ok()
    })
}

#[no_mangle]
pub extern "C" fn dcfg_rp_delete(key: *const c_char) -> bool {
    ffi_guard(|| {
        if !init_sys_prop_once() {
            return false;
        }

        let Some(key) = cstr_to_str(key) else {
            return false;
        };

        resetprop_engine().delete(key).unwrap_or(false)
    })
}

#[no_mangle]
pub extern "C" fn dcfg_rp_get(key: *const c_char, buf: *mut c_char, cap: size_t) -> c_int {
    let result = catch_unwind(AssertUnwindSafe(|| {
        if !init_sys_prop_once() || buf.is_null() || cap == 0 {
            return -1;
        }

        let Some(key) = cstr_to_str(key) else {
            return -1;
        };

        let Some(value) = resetprop_engine().get(key) else {
            return 0;
        };

        let Ok(c_value) = CString::new(value) else {
            return -1;
        };

        let bytes = c_value.as_bytes_with_nul();
        let copy_len = bytes.len().min(cap as usize);

        unsafe {
            ptr::copy_nonoverlapping(bytes.as_ptr(), buf.cast::<u8>(), copy_len);
            if copy_len == cap as usize {
                *buf.add(cap as usize - 1) = 0;
            }
        }

        1
    }));

    result.unwrap_or(-1)
}
