#include "ast/ast.h"
#include "namer/namer.h"

#include <list>
#include <vector>

using namespace std;

namespace ruby_typer {
namespace namer {

class Resolver {
public:
    Resolver(unique_ptr<ast::Statement> tree) : tree_(move(tree)) {}
    void walk(ast::Context);

    unique_ptr<ast::Statement> tree_;
};

struct Nesting {
    unique_ptr<Nesting> parent;
    ast::SymbolRef scope;

    Nesting(unique_ptr<Nesting> parent, ast::SymbolRef scope) : parent(move(parent)), scope(scope) {}

    Nesting(const Nesting &rhs) = delete;
    Nesting(Nesting &&rhs) = delete;
};

class ResolveWalk {
public:
    ResolveWalk(ast::Context ctx, Resolver *resolv)
        : resolv_(resolv), nesting_(make_unique<Nesting>(nullptr, ctx.state.defn_root())) {}

    ast::SymbolRef resolveLhs(ast::Context ctx, ast::NameRef name) {
        Nesting *scope = nesting_.get();
        while (scope != nullptr) {
            auto lookup = scope->scope.info(ctx).findMember(name);
            if (lookup.exists()) {
                return lookup;
            }
            scope = scope->parent.get();
        }
        return ast::SymbolRef(0);
    }

    ast::SymbolRef resolveConstant(ast::Context ctx, ast::ConstantLit *c) {
        if (dynamic_cast<ast::EmptyTree *>(c->scope.get()) != nullptr) {
            return resolveLhs(ctx, c->cnst);
        } else if (ast::ConstantLit *scope = dynamic_cast<ast::ConstantLit *>(c->scope.get())) {

            auto resolved = resolveConstant(ctx, scope);
            if (!resolved.exists())
                return resolved;
            ast::SymbolRef result = resolved.info(ctx).findMember(c->cnst);
            c->scope = make_unique<ast::Ident>(resolved);

            return result;
        } else {
            ctx.state.errors.error(ast::Loc::none(0), ast::ErrorClass::DynamicConstant,
                                   "Dynamic constant references are unsupported");
            return ast::SymbolRef(0);
        }
    }

    ast::ClassDef *preTransformClassDef(ast::Context ctx, ast::ClassDef *original) {
        nesting_ = make_unique<Nesting>(move(nesting_), original->symbol);
        return original;
    }
    ast::Statement *postTransformClassDef(ast::Context ctx, ast::ClassDef *original) {
        nesting_ = move(nesting_->parent);
        return original;
    }

    ast::Statement *postTransformConstantLit(ast::Context ctx, ast::ConstantLit *c) {
        ast::SymbolRef resolved = resolveConstant(ctx, c);
        if (!resolved.exists())
            return c;
        return new ast::Ident(resolved);
    }

    Resolver *resolv_;
    unique_ptr<Nesting> nesting_;
};

void Resolver::walk(ast::Context ctx) {
    ResolveWalk walk(ctx, this);
    tree_ = ast::TreeMap<ResolveWalk>::apply(ctx, walk, move(tree_));
}

unique_ptr<ast::Statement> Namer::resolve(ast::Context &ctx, unique_ptr<ast::Statement> tree) {
    Resolver resolv(move(tree));
    resolv.walk(ctx);
    return move(resolv.tree_);
}
} // namespace namer
} // namespace ruby_typer
