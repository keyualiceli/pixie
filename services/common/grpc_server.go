package common

import (
	"time"

	"github.com/grpc-ecosystem/go-grpc-middleware"
	"github.com/grpc-ecosystem/go-grpc-middleware/auth"
	"github.com/grpc-ecosystem/go-grpc-middleware/logging/logrus"
	"github.com/grpc-ecosystem/go-grpc-middleware/tags"
	log "github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	"golang.org/x/net/context"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/credentials"
	"google.golang.org/grpc/status"
	commonenv "pixielabs.ai/pixielabs/services/common/env"
	"pixielabs.ai/pixielabs/services/common/sessioncontext"
)

func grpcInjectSession() grpc.UnaryServerInterceptor {
	return func(ctx context.Context, req interface{}, info *grpc.UnaryServerInfo, handler grpc.UnaryHandler) (interface{}, error) {
		sCtx := sessioncontext.New()
		return handler(sessioncontext.NewContext(ctx, sCtx), req)
	}
}

func createGRPCAuthFunc(env commonenv.Env) func(context.Context) (context.Context, error) {
	return func(ctx context.Context) (context.Context, error) {
		token, err := grpc_auth.AuthFromMD(ctx, "bearer")
		if err != nil {
			return nil, err
		}

		sCtx, err := sessioncontext.FromContext(ctx)
		if err != nil {
			return nil, status.Errorf(codes.Internal, "missing session context: %v", err)
		}
		err = sCtx.UseJWTAuth(env.JWTSigningKey(), token)
		if err != nil {
			return nil, status.Errorf(codes.Unauthenticated, "invalid auth token: %v", err)
		}
		return ctx, nil
	}
}

// CreateGRPCServer creates a GRPC server with default middleware for our services.
func CreateGRPCServer(env commonenv.Env) *grpc.Server {
	var tlsOpts grpc.ServerOption
	if !viper.GetBool("disable_ssl") {
		// Create the TLS credentials
		tlsCert := viper.GetString("tls_cert")
		tlsKey := viper.GetString("tls_key")
		log.WithFields(log.Fields{
			"tlsCertFile": tlsCert,
			"tlsKeyFile":  tlsKey,
		}).Info("Loading GRPC TLS certs")

		creds, err := credentials.NewServerTLSFromFile(tlsCert, tlsKey)
		if err != nil {
			log.WithError(err).Errorf("Could not load TLS config")
			return nil
		}
		tlsOpts = grpc.Creds(creds)
	}

	logrusEntry := log.NewEntry(log.StandardLogger())
	logrusOpts := []grpc_logrus.Option{
		grpc_logrus.WithDurationField(func(duration time.Duration) (key string, value interface{}) {
			return "time", duration
		}),
	}
	grpc_logrus.ReplaceGrpcLogger(logrusEntry)
	opts := []grpc.ServerOption{
		grpc_middleware.WithUnaryServerChain(
			grpc_ctxtags.UnaryServerInterceptor(),
			grpcInjectSession(),
			grpc_logrus.UnaryServerInterceptor(logrusEntry, logrusOpts...),
			grpc_auth.UnaryServerInterceptor(createGRPCAuthFunc(env)),
		)}
	if !viper.GetBool("disable_ssl") {
		opts = append(opts, tlsOpts)
	}
	grpcServer := grpc.NewServer(opts...)
	return grpcServer
}
