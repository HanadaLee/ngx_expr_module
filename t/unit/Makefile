# Reuse a configured nginx tree, without installing or starting nginx.
# Invoke with -f objs/Makefile -f <module>/t/unit/Makefile.
.PHONY: expr-when-id-test
expr-when-id-test:
	test -n "$(EXPR_MODULE)" && test -n "$(EXPR_TEST_BINARY)"
	$(CC) $(CFLAGS) $(ALL_INCS) -I$(EXPR_MODULE)/src \
		-ffunction-sections -fdata-sections \
		$(EXPR_MODULE)/t/unit/when_id.c $(EXPR_MODULE)/src/ngx_expr.c \
		src/core/ngx_array.c src/core/ngx_palloc.c src/os/unix/ngx_alloc.c \
		-Wl,--gc-sections -o $(EXPR_TEST_BINARY)
	$(EXPR_TEST_BINARY)
