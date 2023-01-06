#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

//===----------------------------------------------------------------------===//
// Lexer
//===----------------------------------------------------------------------===//

// The lexer returns tokens [0-255] if it is an unknown character, otherwise one
// of these for known things.
enum Token {
  tok_eof = -1,
  // commands
  tok_def = -2,
  tok_extern = -3,
  // primary
  tok_identifier = -4,
  tok_number = -5,
};

static std::string IdentifierStr;  // Filled in if tok_identifier
static double NumVal;              // Filled in if tok_number

/// gettok - Return the next token from standard input.
static int gettok() {
  static int LastChar = ' ';
  // Skip any whitespace
  while (isspace(LastChar))
    LastChar = getchar();
  if (isalpha(LastChar)) {  // identifier: [a-zA-Z][a-zA-Z0-9]*
    IdentifierStr = LastChar;
    while (isalnum((LastChar = getchar())))
      IdentifierStr += LastChar;
    if (IdentifierStr == "def")
      return tok_def;
    if (IdentifierStr == "extern")
      return tok_extern;
    return tok_identifier;
  }
  if (isdigit(LastChar) || LastChar == '.') {  // Number: [0-9.]+
    std::string NumStr;
    do {
      NumStr += LastChar;
      LastChar = getchar();

    } while (isdigit(LastChar) || LastChar == '.');
    NumVal = strtod(NumStr.c_str(), 0);
    return tok_number;
  }
  if (LastChar == '#') {
    do
      LastChar = getchar();
    while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');
    if (LastChar != EOF)
      return gettok();
  }
  if (LastChar == EOF)
    return tok_eof;
  int ThisChar = LastChar;
  LastChar = getchar();
  return ThisChar;
}
//===----------------------------------------------------------------------===//
// Abstract Syntax Tree (aka Parse Tree)
//===----------------------------------------------------------------------===//
namespace {
//ExprAST - Base class for all expresion nodes.
class ExprAST {
 public:
  virtual ~ExprAST(){};
};
/// NumberExprAST - Expression class for numeric literals like "1.0".
class NumberExprAST : public ExprAST {
  double Val;

 public:
  NumberExprAST(double Val) : Val(Val) {}
};
//VariableExprAST - Expression class for referencing a variable
class VariableExprAST : public ExprAST {
  std::string Name;

 public:
  VariableExprAST(const std::string& Name) : Name(Name) {}
};
//BinaryExprAST - Expression class for a binary operator
class BinartExprAST : public ExprAST {
  char Op;
  std::unique_ptr<ExprAST> LHS, RHS;

 public:
  BinartExprAST(char op, std::unique_ptr<ExprAST> LHS,
                std::unique_ptr<ExprAST> RHS)
      : Op(op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
};
/// CallExprAST - Expression class for function calls.
class CallExprAST : public ExprAST {
  std::string Callee;
  std::vector<std::unique_ptr<ExprAST>> Args;

 public:
  CallExprAST(const std::string& Callee,
              std::vector<std::unique_ptr<ExprAST>> Args)
      : Callee(Callee), Args(std::move(Args)) {}
};
/// PrototypeAST - This class represents the "prototype" for a function,
/// which captures its name, and its argument names (thus implicitly the number
/// of arguments the function takes).
class PrototypeAST {
  std::string Name;
  std::vector<std::string> Args;

 public:
  PrototypeAST(const std::string& name, std::vector<std::string> Args)
      : Name(name), Args(std::move(Args)) {}
  const std::string& getName() const { return Name; }
};
/// FunctionAST - This class repersents a function definition itself
class FunctionAST {
  std::unique_ptr<PrototypeAST> Proto;
  std::unique_ptr<ExprAST> Body;

 public:
  FunctionAST(std::unique_ptr<PrototypeAST> Proto,
              std::unique_ptr<ExprAST> Body)
      : Proto(std::move(Proto)), Body(std::move(Body)) {}
};
}  // namespace
//===----------------------------------------------------------------------===//
// Parser
//===----------------------------------------------------------------------===//

/// CurTok/getNextToken - Provide a simple token buffer.  CurTok is the current
/// token the parser is looking at.  getNextToken reads another token from the
/// lexer and updates CurTok with its results.
static int CurTok;
static int getNextToken() {
  return CurTok = gettok();
}
/// BinopPrecedence - This holds the precedence for each binary operator that is
/// defined.
static std::map<char, int> BinopPrecedence;

/// GetTokPrecedence - Get the precedence of the pending binary operator token.
static int GetTokPrecedence() {
  if (!isascii(CurTok))
    return -1;

  // Make sure it's a declared binop.
  int TokPrec = BinopPrecedence[CurTok];
  if (TokPrec <= 0)
    return -1;
  return TokPrec;
}
/// LogError* - These are little helper function for error handling.
std::unique_ptr<ExprAST> LogError(const char* Str) {
  fprintf(stderr, "LogError: %s\n", Str);
  return nullptr;
}
std::unique_ptr<PrototypeAST> LogErrorP(const char* Str) {
  LogError(Str);
  return nullptr;
}
static std::unique_ptr<ExprAST> ParseExpression();
/// numberexpr ::= number
static std::unique_ptr<ExprAST> ParseNumberExpr() {
  auto Result = std::make_unique<NumberExprAST>(NumVal);
  getNextToken();  //consume the number
  return std::move(Result);
}
///parenexpr ::= '(' expresion ')'
static std::unique_ptr<ExprAST> ParseParenExpr() {
  getNextToken();  //eat (.
  auto V = ParseExpression();
  if (!V)
    return nullptr;
  if (CurTok != ')')
    return LogError("expected ')'");
  getNextToken();
  return V;
}
/// identifierexpr
///  ::= identifier
///  ::= identifier '(' expresion* ')'
static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
  std::string IdName = IdentifierStr;
  getNextToken();     // eat identifier
  if (CurTok != '(')  // Simple Variable ref.
    return std::make_unique<VariableExprAST>(IdName);
  // Call
  getNextToken();  // eat (
  std::vector<std::unique_ptr<ExprAST>> Args;
  if (CurTok != ')') {
    while (1) {
      if (auto Arg = ParseExpression())
        Args.push_back(std::move(Arg));
      else
        return nullptr;
      if (CurTok == ')')
        break;
      if (CurTok != ',')
        return LogError("Expected ')' or ',' in argument list");
      getNextToken();
    }
  }

  // Eat the ')'.
  getNextToken();

  return std::make_unique<CallExprAST>(IdName, std::move(Args));
}
