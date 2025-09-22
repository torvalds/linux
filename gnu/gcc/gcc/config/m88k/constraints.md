;; Register constraints

(define_register_constraint "x" "XRF_REGS"
  "A register from the 88110 Extended Register File.")

;; Integer constraints

(define_constraint "I"
  "A non-negative 16-bit value."
  (and (match_code "const_int")
       (match_test "SMALL_INTVAL (ival)")))

(define_constraint "J"
  "A non-positive 16-bit value."
  (and (match_code "const_int")
       (match_test "SMALL_INTVAL (-ival)")))

(define_constraint "K"
  "A non-negative value < 32."
  (and (match_code "const_int")
       (match_test "(unsigned HOST_WIDE_INT)ival < 32")))

(define_constraint "L"
  "A constant with only the upper 16-bits set."
  (and (match_code "const_int")
       (match_test "(ival & 0xffff) == 0")))

(define_constraint "M"
  "A constant value that can be formed with `set'."
  (and (match_code "const_int")
       (match_test "integer_ok_for_set(ival)")))

(define_constraint "N"
  "A negative value."
  (and (match_code "const_int")
       (match_test "ival < 0")))

(define_constraint "O"
  "Integer zero."
  (and (match_code "const_int")
       (match_test "ival == 0")))

(define_constraint "P"
  "A positive value."
  (and (match_code "const_int")
       (match_test "ival >= 0")))

;; Floating-point constraints

(define_constraint "G"
  "Floating-point zero."
  (and (match_code "const_double")
       (match_test "hval == 0 && lval == 0")))

;; General constraints

(define_constraint "Q"
  "An address in a call context."
  (match_operand 0 "symbolic_operand"))
