(func $fib (export "fib") (param $n i32) (result i32)
  (local $tmp i32)
  i32.const 1
  set_local $tmp
  block $B0
    get_local $n
    i32.const 2
    i32.lt_s
    br_if $B0
    i32.const 1
    set_local $tmp
    loop $L1
      get_local $n
      i32.const -1
      i32.add
      call $fib
      get_local $tmp
      i32.add
      set_local $tmp
      get_local $n
      i32.const -2
      i32.add
      tee_local $n
      i32.const 1
      i32.gt_s
      br_if $L1
    end
  end
  get_local $tmp)
