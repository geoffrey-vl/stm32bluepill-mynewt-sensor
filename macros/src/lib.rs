//! Mynewt Macros
//! ```
//! clear ; cargo rustc -- -Z unstable-options --pretty expanded | head -20
//! ```

//  Increase recursion limit to prevent quote!{} errors
#![recursion_limit="128"]

extern crate proc_macro;
use proc_macro::TokenStream;
use proc_macro2::Span;
use quote::{quote, quote_spanned};
use syn::{
    parse_macro_input,
    ArgCaptured,
    FnArg::{
        self,
        Captured,
    },
    punctuated::Punctuated,
    spanned::Spanned,
    token::Comma,
};

/// Collect the Wrapped Declarations for all args. Return a TokenStream of the declarations for the wrapper function:
/// `t: Out<os_task>, name: &Strn, func: os_task_func_t, ...`
/// Preserve the span info for error display.
fn collect_wrapped(args: &Vec<TransformedArg>) -> proc_macro2::TokenStream {
    //  Construct a new `TokenStream` to accumulate the expanded code.
    //  We use `TokenStream` instead of string because `TokenStream` remembers the source location (span) in case of errors.
    //  `quote!` returns `proc_macro2::TokenStream` instead of `proc_macro::TokenStream`, so we use `proc_macro2::TokenStream`.
    let mut expanded = proc_macro2::TokenStream::new();
    for arg in args {
        //  Construct the identifier and type tokens, preserving the span: `t: Out<os_task>`
        let TransformedArg{ ident, wrap_type, ident_span, type_span, .. } = arg;
        let ident_token = quote_spanned!(ident_span=> #ident);
        let type_token  = quote_spanned!(type_span => #wrap_type);
        let tokens = quote!{ #ident_token : #type_token };
        //  Append the tokens to the result.
        if !expanded.is_empty() { expanded.extend(quote!{ , }) }
        expanded.extend(tokens);
    }
    //  Return the expanded tokens.
    println!("expanded: {}", expanded);
    expanded
}

/*
t: Out<os_task>,  //  TODO: *mut os_task
name: &Strn,      //  TODO: *const ::cty::c_char
arg: Ptr,         //  TODO: *mut ::cty::c_void
stack_bottom: Out<[os_stack_t]>,  //  TODO: *mut os_stack_t
stack_size: usize,                //  TODO: u16
) -> MynewtResult<()> { //  TODO: ::cty::c_int;

Strn::validate_bytestr(name.bytestr);  //  TODO

name.bytestr.as_ptr() as *const ::cty::c_char,  //  TODO
stack_bottom.as_ptr() as *mut os_stack_t,  //  TODO
stack_size as u16       //  TODO
*/

/// Transformed arg
struct TransformedArg {
    /// Identifier e.g. `name`
    ident: Box<String>,
    /// Original extern type e.g. `*const ::cty::c_char`
    extern_type: Box<String>,
    /// Wrapped type to be exposed e.g. `&Strn`
    wrap_type: Box<String>,
    /// Validation statement e.g. `Strn::validate_bytestr(name.bytestr)`
    validation_stmt: Box<String>,
    /// Call expression e.g. `name.bytestr.as_ptr() as *const ::cty::c_char`
    call_expr: Box<String>,
    /// Span of the identifier (file location)
    ident_span: Box<Span>,
    /// Span of the type (file location)
    type_span: Box<Span>,
}

/// Transform the extern arg for: Wrap declaration, Validation statement and Call expression.
fn transform_arg(arg: ArgCaptured) -> TransformedArg {
    //  `arg` contains `pat : ty`
    //println!("arg: {:#?}", arg);
    let ArgCaptured{ pat, ty, .. } = arg;
    let pat_span = pat.span();
    let ty_span = ty.span();
    //println!("pat: {}", quote!{ #pat });
    //println!("ty: {}",  quote!{ #ty });
    let pat_str = quote!{ #pat }.to_string();
    let ty_str  = quote!{ #ty  }.to_string();
    TransformedArg {
        ident:           Box::new(pat_str.clone()),
        extern_type:     Box::new(ty_str.clone()),
        wrap_type:       Box::new(ty_str.clone()),
        validation_stmt: Box::new("Strn::validate_bytestr(name.bytestr)".to_string()),
        call_expr:       Box::new(pat_str.clone()),
        ident_span:      Box::new(pat_span),
        type_span:       Box::new(ty_span),
    }
}

/// Given a list of extern function arg declarations, return the transformed args.
fn transform_arg_list(args: Punctuated<FnArg, Comma>) -> Vec<TransformedArg>{
    //println!("args: {:#?}", args);
    let mut res = Vec::new();
    for cap in args {
        //println!("cap: {:#?}", cap);
        if let Captured(arg) = cap {
            //  `arg` contains `pat : ty`
            //println!("arg: {:#?}", arg);
            let arg_transformed = transform_arg(arg);
            res.push(arg_transformed);
        } else { assert!(false); }
    }
    res
}

/// Given an `extern "C"` block of function declarations, generate the safe wrapper for the function.
#[proc_macro_attribute]
pub fn safe_wrap(_attr: TokenStream, item: TokenStream) -> TokenStream {
    let namespace = "os";  //  TODO
    let namespace_prefix = format!("{}_", namespace).to_string();  //  e.g. `os_`
    //println!("attr: {:#?}", attr);
    //println!("item: {:#?}", item);
    //  Parse the macro input as an extern "C" function declaration.
    let input = parse_macro_input!(item as syn::ItemForeignMod);
    //println!("input: {:#?}", input);
    //  For each function...
    for foreign_item in input.items {
        //  TODO: Accumulate doc in attrs and rename args.
        //  println!("foreign_item: {:#?}", foreign_item);
        let foreign_item_tokens = quote! { #foreign_item };
        if let syn::ForeignItem::Fn(foreign_fn) = foreign_item {
            //  println!("foreign_fn: {:#?}", foreign_fn);

            //  Get the function name.
            let fn_name = foreign_fn.ident.to_string();
            assert!(fn_name.starts_with(&namespace_prefix));
            let fn_name_without_namespace = &fn_name[namespace_prefix.len()..];
            let fn_name_token = syn::Ident::new(&fn_name, foreign_fn.ident.span());
            let fn_name_without_namespace_token = syn::Ident::new(&fn_name_without_namespace, foreign_fn.ident.span());

            //  Get the function args and transform them.
            let args = foreign_fn.decl.inputs;
            let transformed_args = transform_arg_list(args);

            //  Collect the Wrapped Declarations for all args.
            let wrap_tokens = collect_wrapped(&transformed_args);

            //  TODO: Collect the Validation Statements for all args.

            //  TODO: Collect the Call Expressions for all args.

            let expanded = quote! {
                //  "----------Insert: `pub fn task_init() -> {`----------";
                pub fn #fn_name_without_namespace_token(
                    #wrap_tokens
                    /*
                    t: Out<os_task>,  //  TODO: *mut os_task
                    name: &Strn,      //  TODO: *const ::cty::c_char
                    func: os_task_func_t,
                    arg: Ptr,         //  TODO: *mut ::cty::c_void
                    prio: u8,
                    sanity_itvl: os_time_t,
                    stack_bottom: Out<[os_stack_t]>,  //  TODO: *mut os_stack_t
                    stack_size: usize,                //  TODO: u16
                    */
                ) -> MynewtResult<()> {               //  TODO: ::cty::c_int;
                    "----------Insert Extern: `extern C { pub fn ... }`----------";
                    extern "C" {
                        #foreign_item_tokens
                    }
                    "----------Insert Validation: `Strn::validate_bytestr(name.bytestr)`----------";
                    Strn::validate_bytestr(name.bytestr);  //  TODO
                    unsafe {
                    "----------Insert Call: `let result_code = os_task_init(`----------";
                        let result_code = #fn_name_token(
                            t,
                            name.bytestr.as_ptr() as *const ::cty::c_char,  //  TODO
                            func,
                            arg,
                            prio,
                            sanity_itvl,
                            stack_bottom.as_ptr() as *mut os_stack_t,  //  TODO
                            stack_size as u16       //  TODO
                        );
                        if result_code == 0 { Ok(()) }
                        else { Err(MynewtError::from(result_code)) }
                    }
                }
            };
            //println!("expanded: {:#?}", expanded);
            //  Return the expanded tokens back to the compiler.
            return TokenStream::from(expanded)
        } else { assert!(false) }
        break;
    }
    "fn zzz() {}".parse().unwrap()
}

/// Given a static mutable variable, return an unsafe mutable pointer that's suitable for passing to Mynewt APIs for writing output.
/// ```
/// out!(NETWORK_TASK) = unsafe { &mut NETWORK_TASK }
/// ```
#[proc_macro]
pub fn out(item: TokenStream) -> TokenStream {
    //  Parse the macro input as an identifier e.g. `NETWORK_TASK`.
    let input = parse_macro_input!(item as syn::Ident);
    let ident = input.to_string();
    let expanded = format!(r#"unsafe {{ &mut {} }}"#, ident);
    //  Return the expanded tokens back to the compiler.
    expanded.parse().unwrap()
}

/// Transform input string into a null-terminated bytestring that's suitable for passing to Mynewt APIs
/// ```
/// strn!("network") = &Strn::new( b"network\0" )
/// ```
#[proc_macro]
pub fn strn(item: TokenStream) -> TokenStream {
    //  Parse the macro input as a literal string e.g. `"network"`.
    let input = parse_macro_input!(item as syn::LitStr);
    let val = input.value();
    let expanded = format!(r#"&Strn::new( b"{}\0" )"#, val);
    //  Return the expanded tokens back to the compiler.
    expanded.parse().unwrap()
}

/// Create a null-terminated bytestring `Strn` that's suitable for passing to Mynewt APIs
/// ```
/// init_strn!("network") = Strn{ bytestr: b"network\0" }
/// ```
/// Used like this:
/// ```
/// static STATIC_STRN: Strn = init_strn!("network");
/// let local_strn = init_strn!("network");
/// ```
#[proc_macro]
pub fn init_strn(item: TokenStream) -> TokenStream {
    //  Parse the macro input as a literal string e.g. `"network"`.
    let input = parse_macro_input!(item as syn::LitStr);
    let val = input.value();
    let expanded = format!(r#"Strn{{ bytestr: b"{}\0" }}"#, val);
    //  Return the expanded tokens back to the compiler.
    expanded.parse().unwrap()
}

/// Transform a block of CBOR encoding calls by adding error checking. Given input:
/// ```
/// let encoder = JSON_CONTEXT.encoder("JSON_CONTEXT", "_map");
/// cbor_encode_text_string(
///     encoder,
///     JSON_CONTEXT.key_to_cstr(key_with_opt_null),
///     JSON_CONTEXT.cstr_len(key_with_opt_null));
/// cbor_encode_int(encoder, value);
/// ```
/// Return:
/// ```
/// unsafe {
///     let encoder = JSON_CONTEXT.encoder("JSON_CONTEXT", "_map");
///     let res =
///         tinycbor::cbor_encode_text_string(encoder,
///           JSON_CONTEXT.key_to_cstr(key_with_opt_null),
///           JSON_CONTEXT.cstr_len(key_with_opt_null));
///     JSON_CONTEXT.check_result(res);
///     let res = tinycbor::cbor_encode_int(encoder, value);
///     JSON_CONTEXT.check_result(res);
/// };
/// ```
#[proc_macro]
pub fn run(item: TokenStream) -> TokenStream {
    //  Parse the macro input as a block of statements.
    let input = parse_macro_input!(item as syn::Block);
    //  Construct a new `TokenStream` to accumulate the expanded code.
    //  We use `TokenStream` instead of string because `TokenStream` remembers the source location (span) in case of errors.
    //  `quote!` returns `proc_macro2::TokenStream` instead of `proc_macro::TokenStream`, so we use `proc_macro2::TokenStream`.
    let mut expanded = proc_macro2::TokenStream::new();
    for stmt in input.stmts {  //  For every statement in the block...
        //  Copy the statement into tokens to prevent borrowing problems later.
        let stmt_tokens = quote! { #stmt };
        match stmt {
            //  If this is a statement followed by a semicolon...
            syn::Stmt::Semi(expr, _semi) => {
                match expr {
                    //  If statement is a function call like `func(...)`...
                    syn::Expr::Call(expr) => {                        
                        let func = *expr.func;        //  Get the function called.
                        let func = quote! { #func };  //  Summarise as token form.
                        //  If this is a CBOR encoding call..
                        if func.to_string().starts_with("cbor_encode_") {
                            //  Add error checking to the CBOR statement.
                            let updated_stmt = quote! { 
                                let res = tinycbor::#stmt_tokens;
                                JSON_CONTEXT.check_result(res);
                            };
                            //  Append updated statement tokens to result.
                            expanded.extend(updated_stmt);  
                            continue;  //  Skip to next statement.
                        }
                    }
                    //  If statement is not a function call...
                    _ => {}  //  TODO
                }            
            }
            //  If this is a statement not followed by a semicolon...
            syn::Stmt::Expr(_expr) => {}  //  TOOD
            //  If this is a `let` statement like `let a = ...`...
            syn::Stmt::Local(_local) => {}  //  TODO
            //  If this is an item definition like `const a = ...`...
            syn::Stmt::Item(_item) => {}  //  TODO
        }
        //  If we reach here, this statement is not a CBOR encoding call.  Return verbatim.
        expanded.extend(stmt_tokens);  //  Append statement tokens to result.
    }
    //  Wrap the expanded tokens with an `unsafe` block.
    expanded = quote! {
        unsafe {
            #expanded
        }
    };
    //  Return the expanded tokens back to the compiler.
    TokenStream::from(expanded)
}

// Build the output, possibly using quasi-quotation
// let expanded = quote! {
// ...
// };
// Hand the output tokens back to the compiler
//  TokenStream::from(expanded)

//syn::Expr::Call(syn::ExprCall { func: f, attrs: _, paren_token: _, args: _ }) => {
//  println!("expr2: {:#?}", quote! { #expr2 });
//  println!("ident: {:#?}", quote! { #expr2.func });       
//  if f.ident == "aaa" { println!("f: {:#?}", quote! { #f }); } 
