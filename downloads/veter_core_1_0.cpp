#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include <limits>

// Veter Compiler v0.8 — C++20
// Features: integers, strings, input, arithmetic precedence, comparisons, logical AND/OR/NOT,
// if/else blocks, repeat loops, bytecode listing, and VM execution.
//
// Syntax:
// make x 10
// say x
// take name
// check x >= 10
//   say "large"
// otherwise
//   say "small"
// finish
// repeat 3 times
//   say "hello"
// end repeat

enum class TK { Word, Number, String, Plus, Minus, Star, Slash, LParen, RParen,
                EqEq, NotEq, Less, LessEq, Greater, GreaterEq, Percent, End };
struct Token { TK kind; std::string text; int line; };

static std::vector<Token> lex_line(const std::string& s, int line) {
    std::vector<Token> out;
    for (std::size_t i=0; i<s.size();) {
        char c=s[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
        if (c=='#') break;
        if (c=='"' || c=='\'') {
            char q=c; ++i; std::string v; bool closed=false;
            while(i<s.size()) {
                char d=s[i++];
                if(d==q){closed=true;break;}
                if(d=='\\' && i<s.size()){
                    char e=s[i++];
                    if(e=='n')v+='\n'; else if(e=='t')v+='\t';
                    else if(e=='\\'||e=='"'||e=='\'')v+=e;
                    else {v+='\\';v+=e;}
                } else v+=d;
            }
            if(!closed) throw std::runtime_error("Line "+std::to_string(line)+": unterminated string");
            out.push_back({TK::String,v,line}); continue;
        }
        if(std::isalpha(static_cast<unsigned char>(c))||c=='_'){
            auto st=i++;
            while(i<s.size()&&(std::isalnum(static_cast<unsigned char>(s[i]))||s[i]=='_'))++i;
            out.push_back({TK::Word,s.substr(st,i-st),line});continue;
        }
        if(std::isdigit(static_cast<unsigned char>(c))){
            auto st=i++;
            while(i<s.size()&&std::isdigit(static_cast<unsigned char>(s[i])))++i;
            out.push_back({TK::Number,s.substr(st,i-st),line});continue;
        }
        auto two = i+1<s.size()?s.substr(i,2):std::string();
        if(two=="=="){out.push_back({TK::EqEq,two,line});i+=2;continue;}
        if(two=="!="){out.push_back({TK::NotEq,two,line});i+=2;continue;}
        if(two=="<="){out.push_back({TK::LessEq,two,line});i+=2;continue;}
        if(two==">="){out.push_back({TK::GreaterEq,two,line});i+=2;continue;}
        TK k;
        switch(c){
            case '+':k=TK::Plus;break; case '-':k=TK::Minus;break;
            case '*':k=TK::Star;break; case '/':k=TK::Slash;break; case '%':k=TK::Percent;break;
            case '(':k=TK::LParen;break; case ')':k=TK::RParen;break;
            case '<':k=TK::Less;break; case '>':k=TK::Greater;break;
            default: throw std::runtime_error("Line "+std::to_string(line)+": unexpected character '"+c+"'");
        }
        out.push_back({k,std::string(1,c),line});++i;
    }
    out.push_back({TK::End,"",line});
    return out;
}

using Value=std::variant<long long,std::string>;
enum class Op { PushInt, PushString, Load, Store, ReadInput, Add, Sub, Mul, Div, Mod,
                Eq, Ne, Lt, Le, Gt, Ge, BoolAnd, BoolOr, BoolNot, Print, JumpIfFalse, Jump, LoopCheck, Call, Return, Halt };
struct Ins { Op op; long long number=0; std::string text; int target=0; int line=1; };

class Compiler {
public:
    explicit Compiler(const std::string& source) {
        std::istringstream in(source); std::string line; int n=0;
        while(std::getline(in,line)){++n; auto ts=lex_line(line,n); if(ts.size()>1) lines_.push_back({std::move(ts),n});}
    }
    std::vector<Ins> compile(){
        while(pos_<lines_.size()) statement();
        if(!blocks_.empty()) throw std::runtime_error("Line "+std::to_string(blocks_.back().line)+": block was not closed");
        code_.push_back({Op::Halt,0,"",0, lines_.empty()?1:lines_.back().n});
        for (const auto& call : pending_calls_) {
            auto it = functions_.find(call.second);
            if (it == functions_.end()) throw std::runtime_error("Unknown function '" + call.second + "'");
            code_.at(static_cast<std::size_t>(call.first)).target = it->second;
        }
        return code_;
    }
private:
    struct Line { std::vector<Token> t; int n; };
    struct Block { enum Kind{If,Else,Repeat,Keep,Function} kind; int patch; int start; int line; std::string name; std::vector<int> breaks; };
    std::vector<Line> lines_; std::size_t pos_=0; std::vector<Ins> code_; std::vector<Block> blocks_;
    std::unordered_map<std::string,int> functions_; std::vector<std::pair<int,std::string>> pending_calls_;
    Line& cur(){return lines_.at(pos_);}
    Token& tok(std::size_t i){return cur().t.at(i);}
    std::size_t count(){return cur().t.size()-1;}
    bool isword(std::size_t i,const std::string& s){return i<count()&&tok(i).kind==TK::Word&&tok(i).text==s;}
    void require_end(std::size_t i){if(i<count())throw std::runtime_error("Line "+std::to_string(cur().n)+": unexpected token '"+tok(i).text+"'");}
    void emit(Op op,long long num=0,std::string text="",int target=0,int line=1){code_.push_back({op,num,std::move(text),target,line});}
    int precedence(TK k){if(k==TK::Plus||k==TK::Minus)return 10;if(k==TK::Star||k==TK::Slash||k==TK::Percent)return 20;return -1;}
    std::size_t expr_pos_=0;
    void atom(const std::vector<Token>& t,int line){
        if(t[expr_pos_].kind==TK::Number){emit(Op::PushInt,std::stoll(t[expr_pos_].text),"",0,line);++expr_pos_;}
        else if(t[expr_pos_].kind==TK::String){emit(Op::PushString,0,t[expr_pos_].text,0,line);++expr_pos_;}
        else if(t[expr_pos_].kind==TK::Word){emit(Op::Load,0,t[expr_pos_].text,0,line);++expr_pos_;}
        else if(t[expr_pos_].kind==TK::LParen){
            ++expr_pos_; expression(t,0,line);
            if(t[expr_pos_].kind!=TK::RParen)throw std::runtime_error("Line "+std::to_string(line)+": expected ')'");
            ++expr_pos_;
        } else throw std::runtime_error("Line "+std::to_string(line)+": expected a value");
    }
    void expression(const std::vector<Token>& t,int minp,int line){
        atom(t,line);
        while(precedence(t[expr_pos_].kind)>=minp){
            TK k=t[expr_pos_++].kind; int p=precedence(k);
            expression(t,p+1,line);
            Op op=k==TK::Plus?Op::Add:k==TK::Minus?Op::Sub:k==TK::Star?Op::Mul:k==TK::Slash?Op::Div:Op::Mod;
            emit(op,0,"",0,line);
        }
    }
    std::size_t compile_expr(std::size_t start, int line){
        expr_pos_=start; expression(cur().t,0,line); return expr_pos_;
    }
    int logical_precedence(const std::string& op) { return op=="or" ? 1 : 2; }
    void emit_condition_clause(std::size_t begin, std::size_t end, int line) {
        bool negate=false;
        if(begin<end && cur().t[begin].kind==TK::Word && cur().t[begin].text=="not") { negate=true; ++begin; }
        std::size_t cmp=begin;
        while(cmp<end && cur().t[cmp].kind!=TK::EqEq && cur().t[cmp].kind!=TK::NotEq &&
              cur().t[cmp].kind!=TK::Less && cur().t[cmp].kind!=TK::LessEq &&
              cur().t[cmp].kind!=TK::Greater && cur().t[cmp].kind!=TK::GreaterEq) ++cmp;
        if(cmp==begin || cmp>=end) throw std::runtime_error("Line "+std::to_string(line)+": each logical condition needs a comparison");
        expr_pos_=begin; expression(cur().t,0,line);
        if(expr_pos_!=cmp) throw std::runtime_error("Line "+std::to_string(line)+": invalid left side of condition");
        TK cmp_kind=cur().t[cmp].kind;
        expr_pos_=cmp+1; expression(cur().t,0,line);
        if(expr_pos_!=end) throw std::runtime_error("Line "+std::to_string(line)+": invalid right side of condition");
        Op op=cmp_kind==TK::EqEq?Op::Eq:cmp_kind==TK::NotEq?Op::Ne:cmp_kind==TK::Less?Op::Lt:cmp_kind==TK::LessEq?Op::Le:cmp_kind==TK::Greater?Op::Gt:Op::Ge;
        emit(op,0,"",0,line);
        if(negate) emit(Op::BoolNot,0,"",0,line);
    }
    void compile_condition(std::size_t begin, std::size_t end, int line) {
        std::vector<std::string> ops;
        std::vector<std::size_t> starts;
        std::size_t clause_start=begin;
        for(std::size_t i=begin;i<end;++i) {
            if(cur().t[i].kind==TK::Word && (cur().t[i].text=="and" || cur().t[i].text=="or")) {
                if(i==clause_start) throw std::runtime_error("Line "+std::to_string(line)+": missing condition before logical operator");
                emit_condition_clause(clause_start,i,line);
                std::string current=cur().t[i].text;
                while(!ops.empty() && logical_precedence(ops.back())>=logical_precedence(current)) {
                    emit(ops.back()=="and"?Op::BoolAnd:Op::BoolOr,0,"",0,line); ops.pop_back();
                }
                ops.push_back(current); clause_start=i+1;
            }
        }
        if(clause_start>=end) throw std::runtime_error("Line "+std::to_string(line)+": missing condition after logical operator");
        emit_condition_clause(clause_start,end,line);
        while(!ops.empty()) { emit(ops.back()=="and"?Op::BoolAnd:Op::BoolOr,0,"",0,line); ops.pop_back(); }
    }
    void statement(){
        const int line=cur().n;
        if(isword(0,"do") || isword(0,"create")){
            if(count()!=2 || tok(1).kind!=TK::Word) throw std::runtime_error("Line "+std::to_string(line)+": use 'do function_name'");
            const std::string name=tok(1).text;
            if(functions_.count(name)) throw std::runtime_error("Line "+std::to_string(line)+": function already defined '"+name+"'");
            int skip=(int)code_.size(); emit(Op::Jump,0,"",0,line);
            functions_[name]=(int)code_.size();
            blocks_.push_back({Block::Function,skip,0,line,name,{}});
        } else if(isword(0,"run")){
            if(count()!=2 || tok(1).kind!=TK::Word) throw std::runtime_error("Line "+std::to_string(line)+": use 'run function_name'");
            int at=(int)code_.size(); emit(Op::Call,0,tok(1).text,0,line); pending_calls_.push_back({at,tok(1).text});
        } else if(isword(0,"make") || isword(0,"set")){
            if(count()<3||tok(1).kind!=TK::Word)throw std::runtime_error("Line "+std::to_string(line)+": use 'make name value'");
            auto end=compile_expr(2,line); require_end(end);
            emit(Op::Store,0,tok(1).text,0,line);
        } else if(isword(0,"say") || isword(0,"show") || isword(0,"print")){
            if(count()<2)throw std::runtime_error("Line "+std::to_string(line)+": use 'say value'");
            auto end=compile_expr(1,line);require_end(end);emit(Op::Print,0,"",0,line);
        } else if(isword(0,"take") || isword(0,"ask")){
            if(count()!=2||tok(1).kind!=TK::Word)throw std::runtime_error("Line "+std::to_string(line)+": use 'take variable'");
            emit(Op::ReadInput,0,tok(1).text,0,line);
        } else if(isword(0,"check")){
            if(count()<4) throw std::runtime_error("Line "+std::to_string(line)+": use 'check left == right' (supports and/or/not)");
            compile_condition(1,count(),line);
            int at=(int)code_.size();emit(Op::JumpIfFalse,0,"",0,line);blocks_.push_back({Block::If,at,0,line,"", {}});
        } else if(isword(0,"otherwise")){
            if(count()!=1||blocks_.empty()||blocks_.back().kind!=Block::If)throw std::runtime_error("Line "+std::to_string(line)+": 'otherwise' must follow a 'check' block");
            int jump=(int)code_.size();emit(Op::Jump,0,"",0,line);code_[blocks_.back().patch].target=(int)code_.size();blocks_.back()={Block::Else,jump,0,line,"",{}};
        } else if(isword(0,"finish")){
            if(count()!=1||blocks_.empty()||(blocks_.back().kind!=Block::If&&blocks_.back().kind!=Block::Else))throw std::runtime_error("Line "+std::to_string(line)+": unmatched 'finish'");
            auto b=blocks_.back();blocks_.pop_back();code_[b.patch].target=(int)code_.size();
        } else if(isword(0,"repeat")){
            if(count()!=3||tok(1).kind!=TK::Number||!isword(2,"times"))throw std::runtime_error("Line "+std::to_string(line)+": use 'repeat N times'");
            long long n=std::stoll(tok(1).text); if(n<0||n>1000000)throw std::runtime_error("Line "+std::to_string(line)+": repeat count must be 0..1000000");
            emit(Op::PushInt,n,"",0,line); int start=(int)code_.size(); emit(Op::LoopCheck,0,"",0,line);blocks_.push_back({Block::Repeat,start,start,line,"", {}});
        } else if(isword(0,"keep")){
            // Keep repeating while the condition remains true.
            std::size_t opidx=1;
            while(opidx<count()&&tok(opidx).kind!=TK::EqEq&&tok(opidx).kind!=TK::NotEq&&tok(opidx).kind!=TK::Less&&tok(opidx).kind!=TK::LessEq&&tok(opidx).kind!=TK::Greater&&tok(opidx).kind!=TK::GreaterEq)++opidx;
            if(opidx==1||opidx>=count())throw std::runtime_error("Line "+std::to_string(line)+": use 'keep left < right' (also ==, !=, <=, >, >=)");
            int start=(int)code_.size();
            expr_pos_=1; expression(cur().t,0,line);
            if(expr_pos_!=opidx)throw std::runtime_error("Line "+std::to_string(line)+": invalid left side of keep condition");
            TK cmp=tok(opidx).kind; expr_pos_=opidx+1; expression(cur().t,0,line); require_end(expr_pos_);
            Op op=cmp==TK::EqEq?Op::Eq:cmp==TK::NotEq?Op::Ne:cmp==TK::Less?Op::Lt:cmp==TK::LessEq?Op::Le:cmp==TK::Greater?Op::Gt:Op::Ge;
            emit(op,0,"",0,line); int patch=(int)code_.size(); emit(Op::JumpIfFalse,0,"",0,line);
            blocks_.push_back({Block::Keep,patch,start,line,"", {}});
        } else if(isword(0,"break")){
            if(count()!=1) throw std::runtime_error("Line "+std::to_string(line)+": use 'break' by itself");
            // Find the nearest enclosing loop; a break inside an if nested in a loop is allowed.
            int loop_index=-1;
            for(int j=(int)blocks_.size()-1;j>=0;--j){
                if(blocks_[j].kind==Block::Repeat || blocks_[j].kind==Block::Keep){loop_index=j;break;}
                if(blocks_[j].kind==Block::Function) break;
            }
            if(loop_index<0) throw std::runtime_error("Line "+std::to_string(line)+": 'break' must be inside a loop");
            int at=(int)code_.size(); emit(Op::Jump,0,"",0,line);
            blocks_[loop_index].breaks.push_back(at);
        } else if(isword(0,"skip")){
            if(count()!=1) throw std::runtime_error("Line "+std::to_string(line)+": use 'skip' by itself");
            // Skip the rest of this iteration and continue with the nearest enclosing loop.
            int loop_index=-1;
            for(int j=(int)blocks_.size()-1;j>=0;--j){
                if(blocks_[j].kind==Block::Repeat || blocks_[j].kind==Block::Keep){loop_index=j;break;}
                if(blocks_[j].kind==Block::Function) break;
            }
            if(loop_index<0) throw std::runtime_error("Line "+std::to_string(line)+": 'skip' must be inside a loop");
            emit(Op::Jump,0,"",blocks_[loop_index].start,line);
        } else if(isword(0,"end")){
            if(count()==2 && isword(1,"repeat") && !blocks_.empty() && blocks_.back().kind==Block::Repeat){
                auto b=blocks_.back();blocks_.pop_back();emit(Op::Jump,0,"",b.start,line);code_[b.patch].target=(int)code_.size();for(int at:b.breaks)code_[at].target=(int)code_.size();
            } else if(count()==1 && !blocks_.empty() && blocks_.back().kind==Block::Keep){
                auto b=blocks_.back();blocks_.pop_back();emit(Op::Jump,0,"",b.start,line);code_[b.patch].target=(int)code_.size();for(int at:b.breaks)code_[at].target=(int)code_.size();
            } else if(count()==1 && !blocks_.empty() && blocks_.back().kind==Block::Function){
                auto b=blocks_.back();blocks_.pop_back();emit(Op::Return,0,"",0,line);code_[b.patch].target=(int)code_.size();
            } else throw std::runtime_error("Line "+std::to_string(line)+": use 'end repeat', 'end' for keep loops, or 'end' inside a function");
        } else {
            throw std::runtime_error("Line "+std::to_string(line)+": unknown command '"+tok(0).text+"'");
        }
        ++pos_;
    }
};

class VM {
public:
    explicit VM(const std::vector<Ins>& c):code_(c){}
    void run(std::istream& in,std::ostream& out){
        std::vector<Value> stack;std::unordered_map<std::string,Value> vars;std::size_t ip=0;
        auto pop=[&](){if(stack.empty())throw std::runtime_error("VM stack underflow");Value v=stack.back();stack.pop_back();return v;};
        auto num=[&](Value v,int line)->long long{
            if(std::holds_alternative<long long>(v))return std::get<long long>(v);
            const auto&s=std::get<std::string>(v);try{std::size_t n=0;auto x=std::stoll(s,&n);if(n==s.size())return x;}catch(...){}
            throw std::runtime_error("Line "+std::to_string(line)+": expected a number");
        };
        auto compare=[&](Value a,Value b,int line){
            if(a.index()==b.index()){
                if(std::holds_alternative<long long>(a)){auto x=std::get<long long>(a),y=std::get<long long>(b);return x<y?-1:x>y?1:0;}
                const auto&x=std::get<std::string>(a);const auto&y=std::get<std::string>(b);return x<y?-1:x>y?1:0;
            }
            return num(a,line)<num(b,line)?-1:num(a,line)>num(b,line)?1:0;
        };
        std::unordered_map<std::size_t,long long> loop_remaining;
        std::vector<std::size_t> return_addresses;
        while(ip<code_.size()){
            const auto&i=code_[ip++];
            switch(i.op){
                case Op::PushInt:stack.emplace_back(i.number);break;
                case Op::PushString:stack.emplace_back(i.text);break;
                case Op::Load:{auto it=vars.find(i.text);if(it==vars.end())throw std::runtime_error("Line "+std::to_string(i.line)+": undefined variable '"+i.text+"'");stack.push_back(it->second);break;}
                case Op::Store:vars[i.text]=pop();break;
                case Op::ReadInput:{std::string s;if(!std::getline(in,s))throw std::runtime_error("Line "+std::to_string(i.line)+": input ended");vars[i.text]=s;break;}
                case Op::Add:{Value b=pop(),a=pop();if(a.index()==0&&b.index()==0)stack.emplace_back(std::get<long long>(a)+std::get<long long>(b));else if(a.index()==1&&b.index()==1)stack.emplace_back(std::get<std::string>(a)+std::get<std::string>(b));else throw std::runtime_error("Line "+std::to_string(i.line)+": '+' requires two numbers or two strings");break;}
                case Op::Sub:case Op::Mul:case Op::Div:case Op::Mod:{auto b=num(pop(),i.line),a=num(pop(),i.line);if(i.op==Op::Sub)stack.emplace_back(a-b);else if(i.op==Op::Mul)stack.emplace_back(a*b);else{if(!b)throw std::runtime_error("Line "+std::to_string(i.line)+": division by zero");if(i.op==Op::Div)stack.emplace_back(a/b);else stack.emplace_back(a%b);}break;}
                case Op::Eq:case Op::Ne:case Op::Lt:case Op::Le:case Op::Gt:case Op::Ge:{
                    Value b=pop(),a=pop();int c=compare(a,b,i.line);bool r=i.op==Op::Eq?c==0:i.op==Op::Ne?c!=0:i.op==Op::Lt?c<0:i.op==Op::Le?c<=0:i.op==Op::Gt?c>0:c>=0;stack.emplace_back((long long)(r?1:0));break;}
                case Op::BoolAnd:case Op::BoolOr:{
                    auto b=num(pop(),i.line),a=num(pop(),i.line);
                    stack.emplace_back((long long)(i.op==Op::BoolAnd?(a!=0 && b!=0):(a!=0 || b!=0)));
                    break;
                }
                case Op::BoolNot:{auto a=num(pop(),i.line);stack.emplace_back((long long)(a==0));break;}
                case Op::Print:{Value v=pop();if(v.index()==0)out<<std::get<long long>(v);else out<<std::get<std::string>(v);out<<'\n';break;}
                case Op::JumpIfFalse:{auto v=pop();if(num(v,i.line)==0)ip=(std::size_t)i.target;break;}
                case Op::Jump:ip=(std::size_t)i.target;break;
                case Op::LoopCheck:{
                    const std::size_t check_ip=ip-1;
                    auto it=loop_remaining.find(check_ip);
                    if(it==loop_remaining.end()){
                        long long count=num(pop(),i.line);
                        it=loop_remaining.emplace(check_ip,count).first;
                    }
                    if(it->second<=0){loop_remaining.erase(it);ip=(std::size_t)i.target;}
                    else --it->second;
                    break;
                }
                case Op::Call:return_addresses.push_back(ip);ip=(std::size_t)i.target;break;
                case Op::Return:if(return_addresses.empty())return;ip=return_addresses.back();return_addresses.pop_back();break;
                case Op::Halt:return;
            }
        }
    }
private:const std::vector<Ins>& code_;
};

static std::string read_file(const std::string&p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("Cannot open source file: "+p);std::ostringstream s;s<<f.rdbuf();return s.str();}
static std::string escape_listing(const std::string&s){std::string r;for(char c:s){if(c=='\n')r+="\\n";else if(c=='\t')r+="\\t";else if(c=='"')r+="\\\"";else r+=c;}return r;}
static bool emit_listing(const std::string&p,const std::vector<Ins>&code){
    std::ofstream f(p);if(!f)return false;
    for(const auto&i:code){switch(i.op){
        case Op::PushInt:f<<"PUSH_INT "<<i.number;break;case Op::PushString:f<<"PUSH_STRING \""<<escape_listing(i.text)<<"\"";break;
        case Op::Load:f<<"LOAD "<<i.text;break;case Op::Store:f<<"STORE "<<i.text;break;case Op::ReadInput:f<<"READ_INPUT "<<i.text;break;
        case Op::Add:f<<"ADD";break;case Op::Sub:f<<"SUB";break;case Op::Mul:f<<"MUL";break;case Op::Div:f<<"DIV";break;case Op::Mod:f<<"MOD";break;
        case Op::Eq:f<<"EQ";break;case Op::Ne:f<<"NE";break;case Op::Lt:f<<"LT";break;case Op::Le:f<<"LE";break;case Op::Gt:f<<"GT";break;case Op::Ge:f<<"GE";break;case Op::BoolAnd:f<<"AND";break;case Op::BoolOr:f<<"OR";break;case Op::BoolNot:f<<"NOT";break;
        case Op::Print:f<<"PRINT";break;case Op::JumpIfFalse:f<<"JUMP_IF_FALSE "<<i.target;break;case Op::Jump:f<<"JUMP "<<i.target;break;case Op::LoopCheck:f<<"LOOP_CHECK "<<i.target;break;case Op::Call:f<<"CALL "<<i.text<<" "<<i.target;break;case Op::Return:f<<"RETURN";break;case Op::Halt:f<<"HALT";break;}f<<'\n';}
    return(bool)f;
}
// VETER_WEBSITE_BUILDER_V1: small front-end DSL path, separate from bytecode compiler.
static std::string html_escape(const std::string& s) {
    std::string r;
    for (char c : s) { if (c=='&') r += "&amp;"; else if (c=='<') r += "&lt;"; else if (c=='>') r += "&gt;"; else if (c=='\"') r += "&quot;"; else if (c=='\'') r += "&#39;"; else r += c; }
    return r;
}
static std::string quoted_value(const std::string& s) {
    auto a=s.find('\"'); if(a==std::string::npos) return "";
    auto b=s.find('\"',a+1); if(b==std::string::npos) return "";
    return s.substr(a+1,b-a-1);
}
static bool is_website_source(const std::string& source) {
    std::istringstream in(source); std::string line;
    while(std::getline(in,line)) { auto p=line.find_first_not_of(" \t\r"); if(p==std::string::npos || line[p]=='#') continue; return line.compare(p,22,"make a website called ")==0; }
    return false;
}
static void generate_website(const std::string& source, const std::string& input_path) {
    std::string title="Veter Website", pageTitle="Welcome to Veter", bg="white", fg="#111827", font="Arial", padding="32px";
    std::string body, alertButton;
    std::istringstream in(source); std::string line;
    while(std::getline(in,line)) {
        auto p=line.find_first_not_of(" \t\r"); if(p==std::string::npos) continue; line=line.substr(p);
        auto q=quoted_value(line);
        if(line.rfind("make a website called ",0)==0) title=q;
        else if(line.rfind("set page title ",0)==0) pageTitle=q;
        else if(line.rfind("set background color ",0)==0) { auto v=line.substr(21); if(v=="light blue") bg="#dbeafe"; else if(v=="white") bg="white"; else if(v=="black") bg="#111827"; else if(v=="light green") bg="#dcfce7"; else bg=v; }
        else if(line.rfind("set text color ",0)==0) { auto v=line.substr(15); if(v=="dark blue") fg="#1e3a8a"; else if(v=="black") fg="#111827"; else if(v=="white") fg="white"; else fg=v; }
        else if(line.rfind("set font ",0)==0) font=q;
        else if(line.rfind("set page padding ",0)==0) { auto v=line.substr(17); padding=v+"px"; }
        else if(line=="center content") body += "<main class=\"center\">";
        else if(line.rfind("show a title ",0)==0) body += "<h1>"+html_escape(q)+"</h1>";
        else if(line.rfind("show a subtitle ",0)==0) body += "<h2>"+html_escape(q)+"</h2>";
        else if(line.rfind("show text ",0)==0) body += "<p>"+html_escape(q)+"</p>";
        else if(line.rfind("add a text box called ",0)==0) body += "<label>"+html_escape(q)+"<input type=\"text\" placeholder=\""+html_escape(q)+"\"></label>";
        else if(line.rfind("add a password box called ",0)==0) body += "<label>"+html_escape(q)+"<input type=\"password\" placeholder=\""+html_escape(q)+"\"></label>";
        else if(line.rfind("add a checkbox ",0)==0) body += "<label class=\"check\"><input type=\"checkbox\"> "+html_escape(q)+"</label>";
        else if(line.rfind("add a button ",0)==0) body += "<button type=\"button\" onclick=\"veterAction()\">"+html_escape(q)+"</button>";
        else if(line.rfind("when button ",0)==0 && line.find("show alert ")!=std::string::npos) alertButton=quoted_value(line.substr(line.find("show alert ")));
        else if(line.rfind("add a link ",0)==0) { auto first=line.find('\"'); auto second=first==std::string::npos?first:line.find('\"',first+1); auto third=second==std::string::npos?second:line.find('\"',second+1); auto fourth=third==std::string::npos?third:line.find('\"',third+1); if(first!=std::string::npos&&second!=std::string::npos&&third!=std::string::npos&&fourth!=std::string::npos) body += "<p><a href=\""+html_escape(line.substr(third+1,fourth-third-1))+"\" target=\"_blank\" rel=\"noopener\">"+html_escape(line.substr(first+1,second-first-1))+"</a></p>"; }
    }
    if(body.find("<main class=\"center\">")!=std::string::npos) body += "</main>";
    std::string output=input_path+".html"; std::ofstream f(output,std::ios::binary); if(!f) throw std::runtime_error("Could not write website: "+output);
    f << "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>" << html_escape(pageTitle) << "</title><style>body{margin:0;padding:" << padding << ";background:" << bg << ";color:" << fg << ";font-family:'" << html_escape(font) << "',sans-serif} .center{text-align:center;max-width:760px;margin:0 auto}label{display:block;margin:14px auto;max-width:360px;text-align:left}input:not([type=checkbox]){box-sizing:border-box;display:block;width:100%;padding:12px;margin-top:6px;border:1px solid #94a3b8;border-radius:8px}button{padding:12px 20px;margin:12px;border:0;border-radius:8px;background:#2563eb;color:white;cursor:pointer}a{color:inherit}</style></head><body>" << body << "<script>function veterAction(){alert(" << (alertButton.empty()?"'Hello from Veter!'":("'"+html_escape(alertButton)+"'")) << ");}</script></body></html>";
    if(!f) throw std::runtime_error("Failed writing website: "+output);
    std::cout << "Website generated: " << output << "\\n";
}

// Veter Core 1.0 front-end: original readable command forms lowered to the existing VM IR.
// This is a source-to-source front-end step; legacy syntax remains supported.
static std::string trim_copy(const std::string& s) {
    const auto first=s.find_first_not_of(" \t\r");
    if(first==std::string::npos) return "";
    const auto last=s.find_last_not_of(" \t\r");
    return s.substr(first,last-first+1);
}
static std::string veter_core_frontend(const std::string& source) {
    std::istringstream input(source);
    std::ostringstream output;
    std::string line;
    std::vector<std::string> blocks;
    int line_number=0;
    while(std::getline(input,line)) {
        ++line_number;
        std::string trimmed=trim_copy(line);
        const auto indent=line.find_first_not_of(" \t");
        const std::string pad=indent==std::string::npos ? "" : line.substr(0,indent);
        std::string translated=trimmed;
        auto starts=[&](const std::string& prefix) {
            return translated.compare(0,prefix.size(),prefix)==0;
        };
        if(starts("remember ")) {
            // remember score as 95  -> make score 95
            const auto as=translated.find(" as ",9);
            if(as==std::string::npos || as==9)
                throw std::runtime_error("Line "+std::to_string(line_number)+": use 'remember name as value'");
            translated="make "+translated.substr(9,as-9)+" "+translated.substr(as+4);
        } else if(starts("revise ")) {
            // revise score as score + 1 -> set score score + 1
            const auto as=translated.find(" as ",7);
            if(as==std::string::npos || as==7)
                throw std::runtime_error("Line "+std::to_string(line_number)+": use 'revise name as value'");
            translated="set "+translated.substr(7,as-7)+" "+translated.substr(as+4);
        } else if(starts("display ")) {
            translated="say "+translated.substr(8);
        } else if(starts("ask for ")) {
            translated="take "+translated.substr(8);
        } else if(starts("choose when ")) {
            translated="check "+translated.substr(12);
            blocks.push_back("choice");
        } else if(translated=="else choose") {
            translated="otherwise";
        } else if(starts("repeat ") && translated.size()>7 &&
                  translated.find(" rounds")!=std::string::npos &&
                  translated.substr(translated.size()-7)==" rounds") {
            translated="repeat "+translated.substr(7,translated.size()-14)+" times";
            blocks.push_back("repeat");
        } else if(starts("while ")) {
            translated="keep "+translated.substr(6);
            blocks.push_back("while");
        } else if(translated=="close choice") {
            if(blocks.empty() || blocks.back()!="choice")
                throw std::runtime_error("Line "+std::to_string(line_number)+": 'close choice' has no matching 'choose when'");
            blocks.pop_back();
            translated="finish";
        } else if(translated=="close cycle") {
            if(blocks.empty() || (blocks.back()!="repeat" && blocks.back()!="while"))
                throw std::runtime_error("Line "+std::to_string(line_number)+": 'close cycle' has no matching loop");
            const std::string kind=blocks.back(); blocks.pop_back();
            translated=(kind=="repeat") ? "end repeat" : "end";
        }
        output << pad << translated << '\n';
    }
    if(!blocks.empty())
        throw std::runtime_error("Veter Core: block opened with '" + blocks.back() + "' was not closed");
    return output.str();
}

int main(int argc,char**argv){
    if(argc<2||argc>3){std::cerr<<"Veter Compiler Core 1.0\nUsage: veterc <source.vtr> [--emit-bytecode]\n";return 2;}
    try{const std::string source=read_file(argv[1]);if(is_website_source(source)){generate_website(source,argv[1]);return 0;}const std::string core_source=veter_core_frontend(source);Compiler c(core_source);auto code=c.compile();if(argc==3&&std::string(argv[2])=="--emit-bytecode"){
        std::string out=std::string(argv[1])+".vbc";if(!emit_listing(out,code))throw std::runtime_error("Could not write bytecode file");std::cout<<"Bytecode written: "<<out<<'\n';
    }else{VM vm(code);vm.run(std::cin,std::cout);}}
    catch(const std::exception&e){std::cerr<<"Veter compiler error: "<<e.what()<<'\n';return 1;}return 0;
}


